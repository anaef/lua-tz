/*
 * Provides the Lua TZ module. See LICENSE for license terms.
 */

#include "tz.h"
#include <stdlib.h>
#include <stdint.h>
#ifdef __APPLE__
#	include <libkern/OSByteOrder.h>
#	define be32toh OSSwapBigToHostInt32
#	define be64toh OSSwapBigToHostInt64
#else
#	include <endian.h>
#endif
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <lauxlib.h>

/**
 * TZ header.
 */
struct tzheader {
	char magic[4];
	char version;
	char reserved[15];
	int32_t isgmtcnt;
	int32_t isstdcnt;
	int32_t leapcnt;
	int32_t timecnt;
	int32_t typecnt;
	int32_t charcnt;
};

/**
 * TZ type.
 */
struct tztype {
	int32_t gmtoff;
	int8_t isdst;
	uint8_t abbrind;
};

/**
 * Packed size of TZ type.
 */
#define TZTYPE_PACKED (size_t)(6)

/**
 * TZ data.
 */
struct tzdata {
	struct tzheader header;
	int64_t *timevalues;  /* header.timecnt */
	uint8_t *timetypes;   /* header.timecnt */
	struct tztype *types; /* header.typecnt */
	char *chars;          /* header.charcnt */
	struct tztype *dfltype;
};

/**
 * TZ component.
 */
typedef LUATZ_COMPONENT tzcomponent;

/**
 * Frees TZ data.
 */
static int tz_free (lua_State *L) {
	struct tzdata *tzdata;

	tzdata = luaL_checkudata(L, 1, LUATZ_METATABLE);
	free(tzdata->timevalues);
	free(tzdata->timetypes);
	free(tzdata->types);
	free(tzdata->chars);
	return 0;
}
	
/**
 * Returns the string representation of TZ data.
 */
static int tz_tostring (lua_State *L) {
	struct tzdata *tzdata;

	tzdata = luaL_checkudata(L, 1, LUATZ_METATABLE);
	lua_pushfstring(L, "tz (%p)", tzdata);
	return 1;
}

/**
 * Reads a TZ file header.
 */
static void tz_readheader (lua_State *L, FILE *f, off_t size,
		struct tzheader *header) {
	/* read and check header */
	if (fread(header, sizeof(struct tzheader), 1, f) != 1) {
		fclose(f);
		luaL_error(L, "cannot read TZ file header");
	}
	if (strncmp(header->magic, "TZif", 4) != 0) {
		fclose(f);
		luaL_error(L, "TZ file magic mismatch");
	}
	if (header->version != '\0' && header->version != '2'
			&& header->version != '3') {
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

/**
 * Reads a TZ file and pushes it onto the stack.
 */
static void tz_read (lua_State *L, const char *filename, off_t size) {
	struct tzdata *data;
	struct tzheader *header;
	FILE *f;
	int read64;
	int i;
	int32_t *timevalue;
	char *type;

	/* allocate userdata */
	data = lua_newuserdata(L, sizeof(struct tzdata));
	header = &data->header;
	memset(data, 0, sizeof(struct tzdata));
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
		if (fseek(f, header->timecnt * (sizeof(int32_t)
				+ sizeof(uint8_t))
				+ header->typecnt * (sizeof(int32_t)
				+ sizeof(int8_t) + sizeof(uint8_t))
				+ header->charcnt * sizeof(char)
				+ header->leapcnt * (sizeof(int32_t)
				+ sizeof(int32_t))
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
	if (!(data->timevalues && data->timetypes && data->types
			&& data->chars)) {
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
		for (i = 0; i < data->header.timecnt; i++) {
			data->timevalues[i] = be64toh(data->timevalues[i]);
		}
	} else {
		timevalue = ((int32_t *)data->timevalues) + header->timecnt;
		for (i = header->timecnt - 1; i >= 0; i--) {
			timevalue--;
			data->timevalues[i] = be32toh(*timevalue);
		}
	}
	for (i = 0; i < data->header.timecnt; i++) {
		if (data->timetypes[i] >= header->typecnt) {
			luaL_error(L, "malformed TZ file");
		}
	}
	type = ((char *)data->types) + header->typecnt * TZTYPE_PACKED;
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

/**
 * Pushes TZ data onto the stack, reading it as needed.
 */
static struct tzdata *tz_data (lua_State *L, const char *timezone,
		size_t timezonelen) {
	size_t i;
	char filename[128];
	struct stat buf;

	/* get from TZ table */
	lua_getfield(L, LUA_REGISTRYINDEX, LUATZ_KEY);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, LUATZ_KEY);
	}
	lua_getfield(L, -1, timezone);
	if (!lua_isnil(L, -1)) {
		lua_remove(L, -2);
		return lua_touserdata(L, -1);
	}
	lua_pop(L, 1);

	/* local time or generic? */
	if (timezonelen == sizeof(LUATZ_LOCALTIME) - 1 && memcmp(timezone,
			LUATZ_LOCALTIME, timezonelen) == 0) {
		/* local time */
		memcpy(filename, LUATZ_LOCALFILE, sizeof(LUATZ_LOCALFILE));
	} else {
		/* check timezone length */
		if (timezonelen > sizeof(filename) - sizeof(LUATZ_ZONEINFO)) {
			luaL_error(L, "timezone too large");
		}

		/* make sure we do not read an arbitrary file */
		for (i = 0; i < timezonelen; i++) {
			if (!isalnum(timezone[i]) && (!ispunct(timezone[i])
					|| timezone[i] == '.')) {
				luaL_error(L, "malformed timezone " LUA_QS,
						timezone);
			}
		}

		/* make filename */
		memcpy(filename, LUATZ_ZONEINFO, sizeof(LUATZ_ZONEINFO) - 1);
		memcpy(filename + sizeof(LUATZ_ZONEINFO) - 1, timezone,
				timezonelen + 1);
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

	lua_remove(L, -2);
	return lua_touserdata(L, -1);
}

/**
 * Finds an returns the TZ type at a given time.
 */
static struct tztype *tz_find (struct tzdata *data, int64_t t, int isdst,
		int inverse) {
	int l, u, m;

	l = 0;
	u = data->header.timecnt - 1;
	if (!inverse) {
		while (l <= u) {
			m = (l + u) / 2;
			if (data->timevalues[m] <= t) {
				l = m + 1;
			} else {
				u = m - 1;
			}
		}
	} else {
		while (l <= u) {
			m = (l + u) / 2;
			if (data->timevalues[m] <= t - data->types[
					data->timetypes[m]].gmtoff) {
				l = m + 1;
			} else {
				u = m - 1;
			}
		}
	}
	
	/* match DST as needed */
	if (isdst >= 0) {
		while (u >= 0 && data->types[data->timetypes[u]].isdst
				!= isdst) {
			u--;
		}
	}

	/* return the default type (if any) if not found */
	if (u < 0) {
		return data->dfltype;
	}

	return &data->types[data->timetypes[u]];
}

/**
 * Returns the timezone type of a time and zone.
 */
static int tz_type (lua_State *L) {
	int64_t t;
	const char *timezone;
	size_t timezonelen;
	struct tzdata *data;
	struct tztype *type;

	/* check arguments */
	if (lua_isnoneornil(L, 1)) {
		t = (int64_t)time(NULL);
	} else {
		t = (int64_t)luaL_checknumber(L, 1);
	}
	timezone = luaL_optlstring(L, 2, LUATZ_LOCALTIME, &timezonelen);

	/* get timezone data */
	data = tz_data(L, timezone, timezonelen);

	/* find type */
	type = tz_find(data, t, -1, 0);
	if (!type) {
		return 0;
	}

	/* return time type */
	lua_pushinteger(L, type->gmtoff);
	lua_pushboolean(L, type->isdst);
	lua_pushstring(L, &data->chars[type->abbrind]);
	return 3;
}

/**
 * Days per month.
 */
static const int dayspermonth[2][12] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

/**
 * Returns the number of days in a month.
 */
static int days (tzcomponent year, tzcomponent month) {
	int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	return dayspermonth[leap][month - 1];
}

/**
 * Sets an integer field.
 */
static void setfield (lua_State *L, const char *key, tzcomponent value) {
	lua_pushinteger(L, (lua_Integer)value);
	lua_setfield(L, -2, key);
}

/**
 * Returns a date.
 */
static int tz_date (lua_State *L) {
	const char *format, *timezone;
	int64_t t;
	size_t timezonelen;
	char buffer[256];
	struct tzdata *data;
	struct tztype *type;
	tzcomponent sec, min, hour, day, month, year, wday, yday;
	tzcomponent jd, l, n, i, j;

	/* process arguments */
	format = luaL_optstring(L, 1, "%c");
	if (lua_isnoneornil(L, 2)) {
		t = (int64_t)time(NULL);
	} else {
		t = (int64_t)luaL_checknumber(L, 2);
	}
	timezone = luaL_optlstring(L, 3, LUATZ_LOCALTIME, &timezonelen);
	if (*format == '!') {
		timezone = LUATZ_UTC;
		timezonelen = sizeof(LUATZ_UTC) - 1;
		format++;
	}

	/* get timezone data, find type, and apply offset */
	data = tz_data(L, timezone, timezonelen);
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
		jd = t / 86400 + LUATZ_EPOCH; /* Julian day */
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
			struct tm tm;

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
#ifdef _BSD_SOURCE
				tm.tm_gmtoff = type->gmtoff;
				tm.tm_zone = &data->chars[type->abbrind];
#endif /* _BSD_SOURCE */
			} else {
				tm.tm_isdst = -1;
#ifdef _BSD_SOURCE
				tm.tm_gmtoff = 0;
				tm.tm_zone = "";
#endif /* _BSD_SOURCE */
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

/**
 * Gets an integer field.
 */
static tzcomponent getfield (lua_State *L, int index, const char *key,
		tzcomponent d) {
	tzcomponent value;

	lua_getfield(L, index, key);
	if (lua_isnumber(L, -1)) {
		value = (tzcomponent)lua_tointeger(L, -1);
	} else if (lua_isnil(L, -1)) {
		if (d < 0) {
			luaL_error(L, "field " LUA_QS " is missing", key);
		}
		value = d;
	} else {
		luaL_error(L, "field " LUA_QS " has wrong type (number "
				"expected, got %s)", key, luaL_typename(L, -1));
		return 0; /* not reached */
	}
	lua_pop(L, 1);
	return value;
}

/**
 * Returns a time.
 */
static int tz_time (lua_State *L) {
	int64_t t;
	const char *timezone;
	size_t timezonelen;
	tzcomponent sec, min, hour, day, month, year;
	int isdst, hasoff;
	struct tzdata *data;
	struct tztype *type;

        if (lua_isnoneornil(L, 1)) {
		t = (int64_t)time(NULL);
		if (t == -1) {
			lua_pushnil(L);
			return 1;
		}
	} else {
		/* process arguments */
		luaL_checktype(L, 1, LUA_TTABLE);
		timezone = luaL_optlstring(L, 2, LUATZ_LOCALTIME, &timezonelen);

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
					+ day - 32075 /* Julian day */
					- LUATZ_EPOCH) /* epoch */
					* (int64_t)86400 /* days */
					+ hour * 3600 /* hours */
					+ min * 60 /* minutes */
					+ sec; /* seconds */
		} else {
			lua_pushnil(L);
			return 1;
		}

		/* adjust */
		if (hasoff) {
			t -= getfield(L, 1, "off", -1);
		} else {
			data = tz_data(L, timezone, timezonelen);
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

	lua_pushnumber(L, (lua_Number)t);
	return 1;
}

/*
 * Exported functions.
 */

/* TZ functions */
static const luaL_Reg functions[] = {
        { "type", tz_type },
        { "date", tz_date },
        { "time", tz_time },
        { NULL, NULL }
};

int luaopen_tz (lua_State *L) {
	/* register functions */
	#if LUA_VERSION_NUM >= 502
	luaL_newlib(L, functions);
	#else
	luaL_register(L, luaL_checkstring(L, 1), functions);
	#endif

	/* TZ metatable */	
	luaL_newmetatable(L, LUATZ_METATABLE);
	lua_pushcfunction(L, tz_free);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, tz_tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pop(L, 1);

	return 1;
}
