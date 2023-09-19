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


#define TZ_TYPE_PACKED  (size_t)(6)


struct tz_header {
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

struct tz_type {
	int32_t  gmtoff;
	int8_t   isdst;
	uint8_t  abbrind;
};

struct tz_data {
	struct tz_header  header;
	int64_t          *timevalues;  /* header.timecnt */
	uint8_t          *timetypes;   /* header.timecnt */
	struct tz_type   *types;       /* header.typecnt */
	char             *chars;       /* header.charcnt */
};


static int getfield(lua_State *L, int index, const char *key, int d);
static inline void setfield(lua_State *L, const char *key, int value);
static inline int days(int year, int month);
#if LUA_VERSION_NUM < 502
void *luaL_testudata(lua_State *L, int index, const char *name);
#endif

static int tz_tostring(lua_State *L);
static int tz_gc(lua_State *L);

static void tz_readheader(lua_State *L, FILE *f, off_t size, struct tz_header *header);
static void tz_read(lua_State *L, const char *filename, off_t size);
static struct tz_data *tz_data(lua_State *L, const char *timezone, size_t len);
static struct tz_type *tz_find(struct tz_data *data, int64_t t, int isdst, int reverse);

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

static int getfield (lua_State *L, int index, const char *key, int d) {
	int  value;
#if LUA_VERSION_NUM >= 503
	int  isint;
#endif

	lua_getfield(L, index, key);
	if (lua_isnumber(L, -1)) {
#if LUA_VERSION_NUM >= 503
		value = lua_tointegerx(L, -1, &isint);
		if (!isint) {
			return luaL_error(L, "field '%s' is not an integer", key);
		}
#else
		value = lua_tointeger(L, -1);
#endif
	} else if (lua_isnil(L, -1)) {
		if (d < 0) {
			luaL_error(L, "field '%s' is missing", key);
		}
		value = d;
	} else {
		return luaL_error(L, "field '%s' has wrong type (number expected, got %s)",
				key, luaL_typename(L, -1));
	}
	lua_pop(L, 1);
	return value;
}

static inline void setfield (lua_State *L, const char *key, int value) {
	lua_pushinteger(L, value);
	lua_setfield(L, -2, key);
}

static inline int days (int year, int month) {
	return DAYS_PER_MONTH[year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)][month - 1];
}

#if LUA_VERSION_NUM < 502
void *luaL_testudata (lua_State *L, int index, const char *name) {
	void  *userdata;

	userdata = lua_touserdata(L, index);
	if (!userdata || !lua_getmetatable(L, index)) {
		return NULL;
	}
	luaL_getmetatable(L, name);
	if (!lua_rawequal(L, -1, -2)) {
		userdata = NULL;
	}
	lua_pop(L, 2);
	return userdata;
}
#endif


/*
 * TZ data
 */

static int tz_tostring (lua_State *L) {
	struct tz_data  *data;

	data = luaL_checkudata(L, 1, LUATZ_METATABLE);
	lua_pushfstring(L, LUATZ_METATABLE ": %p", data);
	return 1;
}

static int tz_gc (lua_State *L) {
	struct tz_data  *data;

	data = luaL_checkudata(L, 1, LUATZ_METATABLE);
	free(data->timevalues);
	free(data->timetypes);
	free(data->types);
	free(data->chars);
	return 0;
}


/*
 * zoneinfo
 */

static void tz_readheader (lua_State *L, FILE *f, off_t size, struct tz_header *header) {
	/* read and check header */
	if (fread(header, sizeof(struct tz_header), 1, f) != 1) {
		fclose(f);
		luaL_error(L, "cannot read TZ file header");
	}
	if (strncmp(header->magic, "TZif", 4) != 0) {
		fclose(f);
		luaL_error(L, "TZ file magic mismatch");
	}
	if (header->version != '\0' && header->version != '2' && header->version != '3') {
		fclose(f);
		luaL_error(L, "unsupported TZ file version");
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
			|| (size_t)header->typecnt > size / TZ_TYPE_PACKED
			|| (size_t)header->charcnt > size / sizeof(char)
			|| header->typecnt == 0) {
		fclose(f);
		luaL_error(L, "malformed TZ file");
	}
}

static void tz_read (lua_State *L, const char *filename, off_t size) {
	int               i, read64;
	char             *type;
	FILE             *f;
	int32_t          *timevalue32;
	struct tz_data   *data;
	struct tz_header *header;

	/* allocate userdata */
	data = lua_newuserdata(L, sizeof(struct tz_data));
	memset(data, 0, sizeof(struct tz_data));
	luaL_getmetatable(L, LUATZ_METATABLE);
	lua_setmetatable(L, -2);
	header = &data->header;

	/* read and process header */
	f = fopen(filename, "r");
	if (!f) {
		luaL_error(L, "cannot open TZ file '%s'", filename);
	}
	tz_readheader(L, f, size, header);

	/* use 64-bit structure? */
	read64 = header->version >= '2';
	if (read64) {
		if (fseek(f, header->timecnt * (sizeof(int32_t) + sizeof(uint8_t))
				+ header->typecnt * TZ_TYPE_PACKED
				+ header->charcnt * sizeof(char)
				+ header->leapcnt * (sizeof(int32_t) + sizeof(int32_t))
				+ header->isstdcnt * sizeof(uint8_t)
				+ header->isgmtcnt * sizeof(uint8_t), SEEK_CUR) != 0) {
			fclose(f);
			luaL_error(L, "cannot read TZ file");
		}
		tz_readheader(L, f, size, header);
	}

	/* allocate */
	data->timevalues = calloc(header->timecnt, sizeof(int64_t));
	data->timetypes = calloc(header->timecnt, sizeof(uint8_t));
	data->types = calloc(header->typecnt, sizeof(struct tz_type));
	data->chars = calloc(header->charcnt, sizeof(char));
	if (!(data->timevalues && data->timetypes && data->types && data->chars)) {
		fclose(f);
		luaL_error(L, "cannot allocate TZ data");
	}

	/* read */
	if ((read64 && fread(data->timevalues, sizeof(int64_t),
			header->timecnt, f) != (size_t)header->timecnt)
			|| (!read64 && fread(data->timevalues, sizeof(int32_t),
			header->timecnt, f) != (size_t)header->timecnt)
			|| fread(data->timetypes, sizeof(uint8_t),
			header->timecnt, f) != (size_t)header->timecnt
			|| fread(data->types, TZ_TYPE_PACKED,
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
			data->timevalues[i] = be32toh(*--timevalue32);
		}
	}
	for (i = 0; i < data->header.timecnt; i++) {
		if (data->timetypes[i] >= header->typecnt) {
			luaL_error(L, "malformed TZ file");
		}
	}
	type = (char *)data->types + header->typecnt * TZ_TYPE_PACKED;
	for (i = header->typecnt - 1; i >= 0; i--) {
		type -= TZ_TYPE_PACKED;
		memmove(&data->types[i], type, TZ_TYPE_PACKED);
		data->types[i].gmtoff = be32toh(data->types[i].gmtoff);
		data->types[i].isdst = !!data->types[i].isdst;
	}
}

static struct tz_data *tz_data (lua_State *L, const char *timezone, size_t len) {
	size_t           i;
	char             filename[128];
	struct stat      buf;
	struct tz_data  *data;

	/* get from TZ table */
	lua_getfield(L, LUA_REGISTRYINDEX, LUATZ_KEY);
	if (lua_type(L, -1) != LUA_TTABLE) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, LUATZ_KEY);
	}
	lua_getfield(L, -1, timezone);
	data = luaL_testudata(L, -1, LUATZ_METATABLE);
	if (data) {
		lua_remove(L, -2);
		return data;
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
			if (!isalnum(timezone[i]) && (!ispunct(timezone[i])
					|| timezone[i] == '.')) {
				luaL_error(L, "malformed timezone '%s'", timezone);
			}
		}

		/* make filename */
		memcpy(filename, LUATZ_ZONEINFO, sizeof(LUATZ_ZONEINFO) - 1);
		memcpy(filename + sizeof(LUATZ_ZONEINFO) - 1, timezone, len + 1);
	}

	/* check file */
	if (stat(filename, &buf) != 0 || !S_ISREG(buf.st_mode)) {
		luaL_error(L, "unknown timezone '%s'", timezone);
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

static struct tz_type *tz_find (struct tz_data *data, int64_t t, int isdst, int reverse) {
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
		if (isdst >= 0  /* isdst is specified */
				&& data->types[data->timetypes[upper]].isdst != isdst  /* not eq */
				&& upper > 0  /* predecessor exists */
				&& data->types[data->timetypes[upper - 1]].isdst == isdst  /* eq */
				&& (t - data->types[data->timetypes[upper]].gmtoff)  /* UTC time */
				- data->timevalues[upper]  /* seconds into new type */
				< (data->types[data->timetypes[upper - 1]].gmtoff
				- data->types[data->timetypes[upper]].gmtoff)) {  /* off decrease */
			upper--;  /* use 'hour a' instead of the default 'hour b' */
		}
	}
	return upper >= 0 ? &data->types[data->timetypes[upper]] : &data->types[0];
}


/*
 * functions
 */

static int tz_info (lua_State *L) {
	size_t           len;
	int64_t          t;
	const char      *timezone;
	struct tz_data  *data;
	struct tz_type  *type;

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

	/* get time zone data, find type, and return time info */
	data = tz_data(L, timezone, len);
	type = tz_find(data, t, -1, 0);
	lua_pushinteger(L, type->gmtoff);
	lua_pushboolean(L, type->isdst);
	lua_pushstring(L, &data->chars[type->abbrind]);
	return 3;
}

static int tz_date (lua_State *L) {
	int              jd, l, n, i, j;
	int              sec, min, hour, day, month, year, wday, yday;
	char             buffer[256];
	size_t           len;
	int64_t          t;
	struct tm        tm;
	const char      *format, *timezone;
	struct tz_data  *data;
	struct tz_type  *type;

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
	t += type->gmtoff;

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
			lua_pushboolean(L, type->isdst);
			lua_setfield(L, -2, "isdst");
			setfield(L, "off", type->gmtoff);
			lua_pushstring(L, &data->chars[type->abbrind]);
			lua_setfield(L, -2, "zone");
		} else {
			tm.tm_sec = sec;
			tm.tm_min = min;
			tm.tm_hour = hour;
			tm.tm_mday = day;
			tm.tm_mon = month - 1;
			tm.tm_year = year - 1900;
			tm.tm_wday = wday - 1;
			tm.tm_yday = yday - 1;
			tm.tm_isdst = type->isdst;
#if defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
			tm.tm_gmtoff = type->gmtoff;
			tm.tm_zone = &data->chars[type->abbrind];
#endif
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
	int              isdst, hastimezone, hasoff;
	int              sec, min, hour, day, month, year;
	size_t           len;
	int64_t          t;
	const char      *timezone;
	struct tz_data  *data;
	struct tz_type  *type;

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
		hastimezone = !lua_isnoneornil(L, 2);

		/* get time in UTC */
		sec = getfield(L, 1, "sec", 0);
		min = getfield(L, 1, "min", 0);
		hour = getfield(L, 1, "hour", 12);
		day = getfield(L, 1, "day", -1);
		month = getfield(L, 1, "month", -1);
		year = getfield(L, 1, "year", -1);
		lua_getfield(L, 1, "isdst");
		isdst = !lua_isnil(L, -1) ? lua_toboolean(L, -1): -1;
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
		if (hasoff && !hastimezone) {
			t -= getfield(L, 1, "off", -1);
		} else {
			data = tz_data(L, timezone, len);
			type = tz_find(data, t, isdst, 1);
			t -= type->gmtoff;
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
