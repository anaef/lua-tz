/*
 * Lua TZ
 *
 * Copyright (C) 2014-2023 Andre Naef 
 */


#ifndef _TZ_INCLUDED
#define _TZ_INCLUDED


#include <lua.h>


#define TZ_LOCALFILE  "/etc/localtime"        /* local time TZ file */
#define TZ_ZONEINFO   "/usr/share/zoneinfo/"  /* path where generic TZ files reside */
#define TZ_LOCALTIME  "localtime"             /* local time zone */
#define TZ_UTC        "UTC"                   /* UTC time zone */
#define TZ_DATA       "tz.data"               /* TZ data metatable */
#define TZ_CACHE      "tz.cache"              /* TZ cache registry key */
#define TZ_EPOCH      2440588                 /* Julian day number of epoch (January 1, 1970) */
#define TZ_J0_TIME    -210866803200           /* Julian day 0 time (November 24, -4713) */
#define TZ_J0_YEAR    -4713                   /* Julian day 0 year (November 24, -4713) */


int luaopen_tz(lua_State *L);


#endif  /* _TZ_INCLUDED */
