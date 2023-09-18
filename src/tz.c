/*
 * Lua TZ
 *
 * Copyright (C) 2014-2023 Andre Naef 
 */


#include "tz.h"
#include <stdlib.h>
#include <stdint.h>
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define be32toh OSSwapBigToHostInt32
#define be64toh OSSwapBigToHostInt64
#else
#include <endian.h>
#endif
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <lauxlib.h>


#define TZTYPE_PACKED  (size_t)(6)


struct tzheader {
	char     magic[4];
	char     version;
	char     reserved[15];
	int32_t  isgmtcnt;
	int32_t  isstdcnt;
	int32_t  leapcnt;
	int32_t  timecnt;
	int32_t  typecnt;
	int32_t  charcnt;
};

struct tztype {
	int32_t  gmtoff;
	int8_t   isdst;
	uint8_t  abbrind;
};

struct tzdata {
	struct tzheader  header;
	int64_t         *timevalues;  /* header.timecnt */
	uint8_t         *timetypes;   /* header.timecnt */
	struct tztype   *types;       /* header.typecnt */
	char            *chars;       /* header.charcnt */
	struct tztype   *dfltype;
};

typedef LUATZ_COMPONENT  tzcomponent;


static tzcomponent getfield(lua_State *L, int index, const char *key, tzcomponent d);
static inline void setfield(lua_State *L, const char *key, tzcomponent value);
static inline int days(tzcomponent year, tzcomponent month);

static int tz_tostring(lua_State *L);
static int tz_gc(lua_State *L);

static void tz_readheader(lua_State *L, FILE *f, off_t size, struct tzheader *header);
static void tz_read(lua_State *L, const char *filename, off_t size);
static struct tzdata *tz_data(lua_State *L, const char *timezone, size_t len);
static struct tztype *tz_find(struct tzdata *data, int64_t t, int isdst, int reverse);

static int tz_info(lua_State *L);
static int tz_date(lua_State *L);
static int tz_time(lua_State *L);


static const int DAYS_PER_MONTH[2][12] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};


/*
 * utilities
 */

static tzcomponent getfield (lua_State *L, int index, const char *key, tzcomponent d) {
	tzcomponent  value;

	lua_getfield(L, index, key);
	if (lua_isnumber(L, -1)) {
		value = (tzcomponent)lua_tointeger(L, -1);
	} else if (lua_isnil(L, -1)) {
		if (d < 0) {
			luaL_error(L, "field " LUA_QS " is missing", key);
		}
		value = d;
	} else {
		luaL_error(L, "field " LUA_QS " has wrong type (number expected, got %s)", key,
				luaL_typename(L, -1));
		return 0;  /* not reached */
	}
	lua_pop(L, 1);
	return value;
}

static inline void setfield (lua_State *L, const char *key, tzcomponent value) {
	lua_pushinteger(L, (lua_Integer)value);
	lua_setfield(L, -2, key);
}

static inline int days (tzcomponent year, tzcomponent month) {
	return DAYS_PER_MONTH[year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)][month - 1];
}


/*
 * tz type
 */

static int tz_tostring (lua_State *L) {
	struct tzdata  *tzdata;

	tzdata = luaL_checkudata(L, 1, LUATZ_METATABLE);
	lua_pushfstring(L, LUATZ_METATABLE ": %p", tzdata);
	return 1;
}

static int tz_gc (lua_State *L) {
	struct tzdata  *tzdata;

	tzdata = luaL_checkudata(L, 1, LUATZ_METATABLE);
	free(tzdata->timevalues);
	free(tzdata->timetypes);
	free(tzdata->types);
	free(tzdata->chars);
	return 0;
}


/*
 * zoneinfo
 */

static void tz_readheader (lua_State *L, FILE *f, off_t size, struct tzheader *header) {
	/* read and check header */
	if (fread(header, sizeof(struct tzheader), 1, f) != 1) {
		fclose(f);
		luaL_error(L, "cannot read TZ file header");
	}
	if (strncmp(header->magic, "TZif", 4) != 0) {
		fclose(f);
		luaL_error(L, "TZ file magic mismatch");
	}
	if (header->version != '\0' && header->version != '2' && header->version != '3') {
		fclose(f);
		luaL_error(L, "TZ file version unsupported");
	}

	/* convert */
	header->isstdcnt = be32toh(header->isstdcnt);
	header->isgmtcnt = be32toh(header->isgmtcnt);
	header->leapcnt = be32toh(header->leapcnt);
	header->timecnt = be32toh(header->timecnt);
	header->typecnt = be32toh(header->typecnt);
	header->charcnt = be32toh(header->charcnt);

	/* sanity checks */
	if ((size_t)header->timecnt > size / sizeof(uint8_t)
			|| (size_t)header->typecnt > size / TZTYPE_PACKED
			|| (size_t)header->charcnt > size / sizeof(char)) {
		fclose(f);
		luaL_error(L, "malformed TZ file");
	}
}

static void tz_read (lua_State *L, const char *filename, off_t size) {
	int              i, read64;
	char            *type;
	FILE            *f;
	int32_t         *timevalue32;
	struct tzdata   *data;
	struct tzheader *header;

	/* allocate userdata */
	data = lua_newuserdata(L, sizeof(struct tzdata));
	memset(data, 0, sizeof(struct tzdata));
	header = &data->header;
	luaL_getmetatable(L, LUATZ_METATABLE);
	lua_setmetatable(L, -2);

	/* read and process header */
	f = fopen(filename, "r");
	if (!f) {
		luaL_error(L, "cannot open TZ file " LUA_QS, filename);
	}
	tz_readheader(L, f, size, header);

	/* use 64-bit structure? */
	read64 = header->version >= '2';
	if (read64) {
		if (fseek(f, header->timecnt * (sizeof(int32_t) + sizeof(uint8_t))
				+ header->typecnt * TZTYPE_PACKED
				+ header->charcnt * sizeof(char)
				+ header->leapcnt * (sizeof(int32_t) + sizeof(int32_t))
				+ header->isstdcnt * sizeof(uint8_t)
				+ header->isgmtcnt * sizeof(uint8_t),
				SEEK_CUR) != 0) {
			fclose(f);
			luaL_error(L, "cannot read TZ file");
		}
		tz_readheader(L, f, size, header);
	}

	/* allocate */
	data->timevalues = calloc(header->timecnt, sizeof(int64_t));
	data->timetypes = calloc(header->timecnt, sizeof(uint8_t));
	data->types = calloc(header->typecnt, sizeof(struct tztype));
	data->chars = calloc(header->charcnt, sizeof(char));
	if (!(data->timevalues && data->timetypes && data->types && data->chars)) {
		fclose(f);
		luaL_error(L, "cannot allocate TZ data memory");
	}

	/* read */
	if ((read64 && fread(data->timevalues, sizeof(int64_t),
			header->timecnt, f) != (size_t)header->timecnt)
			|| (!read64 && fread(data->timevalues, sizeof(int32_t),
			header->timecnt, f) != (size_t)header->timecnt)
			|| fread(data->timetypes, sizeof(uint8_t),
			header->timecnt, f) != (size_t)header->timecnt
			|| fread(data->types, TZTYPE_PACKED,
			header->typecnt, f) != (size_t)header->typecnt
			|| fread(data->chars, sizeof(char),
			header->charcnt, f) != (size_t)header->charcnt) {
		fclose(f);
		luaL_error(L, "cannot read TZ data");
	}
	fclose(f);

	/* process */
	if (read64) {
		for (i = 0; i < header->timecnt; i++) {
			data->timevalues[i] = be64toh(data->timevalues[i]);
		}
	} else {
		timevalue32 = ((int32_t *)data->timevalues) + header->timecnt;
		for (i = header->timecnt - 1; i >= 0; i--) {
			timevalue32--;
			data->timevalues[i] = be32toh(*timevalue32);
		}
	}
	for (i = 0; i < data->header.timecnt; i++) {
		if (data->timetypes[i] >= header->typecnt) {
			luaL_error(L, "malformed TZ file");
		}
	}
	type = (char *)data->types + header->typecnt * TZTYPE_PACKED;
	for (i = header->typecnt - 1; i >= 0; i--) {
		type -= TZTYPE_PACKED;
		memmove(&data->types[i], type, TZTYPE_PACKED);
		data->types[i].gmtoff = be32toh(data->types[i].gmtoff);
		data->types[i].isdst = !!data->types[i].isdst;

		/* set the first non-dst type as default */
		if (!data->types[i].isdst) {
			data->dfltype = &data->types[i];
		}
	}

	/* otherwise, use the very first type (if any) as the default type */	
	if (!data->dfltype && header->typecnt > 0) {
		data->dfltype = &data->types[0];
	}
}

static struct tzdata *tz_data (lua_State *L, const char *timezone, size_t len) {
	size_t          i;
	char            filename[128];
	struct stat     buf;
	struct tzdata  *tzdata;

	/* get from TZ table */
	if (lua_getfield(L, LUA_REGISTRYINDEX, LUATZ_KEY) != LUA_TTABLE) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, LUATZ_KEY);
	}
	if (lua_getfield(L, -1, timezone) == LUA_TUSERDATA) {
		tzdata = luaL_testudata(L, -1, LUATZ_METATABLE);
		if (tzdata) {
			lua_remove(L, -2);
			return tzdata;
		}
	}
	lua_pop(L, 1);

	/* local time or generic? */
	if (len == sizeof(LUATZ_LOCALTIME) - 1 && memcmp(timezone, LUATZ_LOCALTIME, len) == 0) {
		/* local time */
		memcpy(filename, LUATZ_LOCALFILE, sizeof(LUATZ_LOCALFILE));
	} else {
		/* check timezone length */
		if (len > sizeof(filename) - sizeof(LUATZ_ZONEINFO)) {
			luaL_error(L, "timezone too long");
		}

		/* make sure we do not read an arbitrary file */
		for (i = 0; i < len; i++) {
			if (!isalnum(timezone[i]) && (!ispunct(timezone[i]) || timezone[i] == '.')) {
				luaL_error(L, "malformed timezone " LUA_QS, timezone);
			}
		}

		/* make filename */
		memcpy(filename, LUATZ_ZONEINFO, sizeof(LUATZ_ZONEINFO) - 1);
		memcpy(filename + sizeof(LUATZ_ZONEINFO) - 1, timezone, len + 1);
	}

	/* check file */
	if (stat(filename, &buf) != 0 || !S_ISREG(buf.st_mode)) {
		luaL_error(L, "unknown timezone " LUA_QS, timezone);
	}

	/* read */
	tz_read(L, filename, buf.st_size);

	/* cache */
	lua_pushvalue(L, -1);
	lua_setfield(L, -3, timezone);

	/* done */
	lua_remove(L, -2);
	return lua_touserdata(L, -1);
}

static struct tztype *tz_find (struct tzdata *data, int64_t t, int isdst, int reverse) {
	int  lower, upper, mid;

	lower = 0;
	upper = data->header.timecnt - 1;
	if (!reverse) {
		while (lower <= upper) {
			mid = (lower + upper) / 2;
			if (data->timevalues[mid] <= t) {
				lower = mid + 1;
			} else {
				upper = mid - 1;
			}
		}
	} else {
		while (lower <= upper) {
			mid = (lower + upper) / 2;
			if (data->timevalues[mid] <= t - data->types[data->timetypes[mid]].gmtoff) {
				lower = mid + 1;
			} else {
				upper = mid - 1;
			}
		}
		if (isdst >= 0 && data->types[data->timetypes[upper]].isdst != isdst && upper > 0
				&& data->types[data->timetypes[upper - 1]].isdst == isdst
				&& (t - data->types[data->timetypes[upper]].gmtoff)
				- data->timevalues[upper]
				< data->types[data->timetypes[upper - 1]].gmtoff
				- data->types[data->timetypes[upper]].gmtoff) {
			upper--;  /* use 'hour a' instead of the default 'hour b' */
		}
	}

	return upper >= 0 ? &data->types[data->timetypes[upper]] : data->dfltype;
}


/*
 * functions
 */

static int tz_info (lua_State *L) {
	size_t          len;
	int64_t         t;
	const char     *timezone;
	struct tzdata  *data;
	struct tztype  *type;

	/* check arguments */
	if (lua_isnoneornil(L, 1)) {
		t = (int64_t)time(NULL);
	} else {
#if LUA_VERSION_NUM >= 503
		t = (int64_t)luaL_checkinteger(L, 1);
#else
		t = (int64_t)luaL_checknumber(L, 1);
#endif
	}
	timezone = luaL_optlstring(L, 2, LUATZ_LOCALTIME, &len);

	/* get time zone data */
	data = tz_data(L, timezone, len);

	/* find type */
	type = tz_find(data, t, -1, 0);
	if (!type) {
		return 0;
	}

	/* return time info */
	lua_pushinteger(L, type->gmtoff);
	lua_pushboolean(L, type->isdst);
	lua_pushstring(L, &data->chars[type->abbrind]);
	return 3;
}

static int tz_date (lua_State *L) {
	char            buffer[256];
	size_t          len;
	int64_t         t;
	struct tm       tm;
	const char     *format, *timezone;
	tzcomponent     sec, min, hour, day, month, year, wday, yday;
	tzcomponent     jd, l, n, i, j;
	struct tzdata  *data;
	struct tztype  *type;

	/* process arguments */
	format = luaL_optstring(L, 1, "%c");
	if (lua_isnoneornil(L, 2)) {
		t = (int64_t)time(NULL);
	} else {
#if LUA_VERSION_NUM >= 503
		t = (int64_t)luaL_checkinteger(L, 2);
#else
		t = (int64_t)luaL_checknumber(L, 2);
#endif
	}
	timezone = luaL_optlstring(L, 3, LUATZ_LOCALTIME, &len);
	if (*format == '!') {
		timezone = LUATZ_UTC;
		len = sizeof(LUATZ_UTC) - 1;
		format++;
	}

	/* get timezone data, find type, and apply offset */
	data = tz_data(L, timezone, len);
	type = tz_find(data, t, -1, 0);
	if (type) {
		t += type->gmtoff;
	}

	/* make date */
	if (t >= LUATZ_J0_TIME) {
		sec = t % 86400;
		if (sec < 0) {
			sec += 86400;
		}
		hour = sec / 3600;
		sec %= 3600;
		min = sec / 60;
		sec %= 60;
		/* source: Henry F. Fliegel, Thomas C. van Flandern:
		   Letters to the editor: a machine algorithm for processing
		   calendar dates. Commun. ACM 11(10): 657 (1968) */
		if (t < 0) {
			t -= 86399;
		}
		jd = t / 86400 + LUATZ_EPOCH;  /* Julian day */
		l = jd + 68569;
		n = (4 * l) / 146097;
		l = l - (146097 * n + 3) / 4;
		i = (4000 * (l + 1)) / 1461001;
		l = l - (1461 * i) / 4 + 31;
		j = (80 * l) / 2447;
		day = l - (2447 * j) / 80;
		l = j / 11;
		month = j + 2 - (12 * l);
		year = 100 * (n - 49) + i + l;
		wday = (jd + 1) % 7 + 1;
		yday = 0;
		for (i = 1; i < month; i++) {
			yday += days(year, i);
		}
		yday += day;
		if (strcmp(format, "*t") == 0) {
			lua_createtable(L, 0, 11);
			setfield(L, "sec", sec);
			setfield(L, "min", min);
			setfield(L, "hour", hour);
			setfield(L, "day", day);
			setfield(L, "month", month);
			setfield(L, "year", year);
			setfield(L, "wday", wday);
			setfield(L, "yday", yday);
			if (type) {
				lua_pushboolean(L, type->isdst);
				lua_setfield(L, -2, "isdst");
				setfield(L, "off", type->gmtoff);
				lua_pushstring(L, &data->chars[type->abbrind]);
				lua_setfield(L, -2, "zone");
			}
		} else {
			tm.tm_sec = (int)sec;
			tm.tm_min = (int)min;
			tm.tm_hour = (int)hour;
			tm.tm_mday = (int)day;
			tm.tm_mon = (int)(month - 1);
			tm.tm_year = (int)(year - 1900);
			tm.tm_wday = (int)(wday - 1);
			tm.tm_yday = (int)(yday - 1);
			if (type) {
				tm.tm_isdst = type->isdst;
#if defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
				tm.tm_gmtoff = type->gmtoff;
				tm.tm_zone = &data->chars[type->abbrind];
#endif
			} else {
				tm.tm_isdst = -1;
#if defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
				tm.tm_gmtoff = 0;
				tm.tm_zone = "";
#endif
			}
			if (strftime(buffer, sizeof(buffer), format, &tm)) {
				lua_pushstring(L, buffer);
			} else {
				return luaL_error(L, "format too long");
			}
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int tz_time (lua_State *L) {
	int             isdst, hasoff;
	size_t          len;
	int64_t         t;
	const char     *timezone;
	tzcomponent     sec, min, hour, day, month, year;
	struct tzdata  *data;
	struct tztype  *type;

        if (lua_isnoneornil(L, 1)) {
		t = (int64_t)time(NULL);
		if (t == -1) {
			lua_pushnil(L);
			return 1;
		}
	} else {
		/* process arguments */
		luaL_checktype(L, 1, LUA_TTABLE);
		timezone = luaL_optlstring(L, 2, LUATZ_LOCALTIME, &len);

		/* get time in UTC */
		sec = getfield(L, 1, "sec", 0);
		min = getfield(L, 1, "min", 0);
		hour = getfield(L, 1, "hour", 12);
		day = getfield(L, 1, "day", -1);
		month = getfield(L, 1, "month", -1);
		year = getfield(L, 1, "year", -1);
		lua_getfield(L, 1, "isdst");
		isdst = lua_isnil(L, -1) ? -1 : lua_toboolean(L, -1);
		lua_getfield(L, 1, "off");
		hasoff = !lua_isnil(L, -1);
		lua_pop(L, 2);
		if (month < 1) {
			year += (month - 12) / 12;
			month = month % 12 + 12;
		}
		if (month > 12) {
			year += (month - 1) / 12;
			month = (month - 1) % 12 + 1;
		}
		/* source: Henry F. Fliegel, Thomas C. van Flandern:
		   Letters to the editor: a machine algorithm for processing
		   calendar dates. Commun. ACM 11(10): 657 (1968) */
		if (year >= LUATZ_J0_YEAR) {
			t  = ((1461 * (year + 4800 + (month - 14) / 12)) / 4
					+ (367 * (month - 2 - 12 * ((month - 14)
					/ 12))) / 12 - (3 * ((year + 4900
					+ (month - 14) / 12) / 100)) / 4
					+ day - 32075     /* Julian day */
					- LUATZ_EPOCH)    /* epoch */
					* (int64_t)86400  /* days */
					+ hour * 3600     /* hours */
					+ min * 60        /* minutes */
					+ sec;            /* seconds */
		} else {
			lua_pushnil(L);
			return 1;
		}

		/* adjust */
		if (hasoff) {
			t -= getfield(L, 1, "off", -1);
		} else {
			data = tz_data(L, timezone, len);
			type = tz_find(data, t, isdst, 1);
			if (type) {
				t -= type->gmtoff;
			}
		}
		if (t < LUATZ_J0_TIME) {
			lua_pushnil(L);
			return 1;
		}
	}
#if LUA_VERSION_NUM >= 503
	lua_pushinteger(L, (lua_Integer)t);
#else
	lua_pushnumber(L, (lua_Number)t);
#endif
	return 1;
}


/*
 * interface
 */

int luaopen_tz (lua_State *L) {
	static const luaL_Reg functions[] = {
		{ "info", tz_info },
		{ "type", tz_info },  /* legacy */
		{ "date", tz_date },
		{ "time", tz_time },
		{ NULL, NULL }
	};

	/* register functions */
#if LUA_VERSION_NUM >= 502
	luaL_newlib(L, functions);
#else
	luaL_register(L, luaL_checkstring(L, 1), functions);
#endif

	/* TZ metatable */	
	luaL_newmetatable(L, LUATZ_METATABLE);
	lua_pushcfunction(L, tz_tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pushcfunction(L, tz_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	return 1;
}
