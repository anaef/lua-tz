/*
 * Lua TZ
 *
 * Copyright (C) 2014-2023 Andre Naef 
 */


#ifndef _LUATZ_INCLUDED
#define _LUATZ_INCLUDED


#include <lua.h>


#define LUATZ_LOCALFILE  "/etc/localtime"        /* local time TZ file */
#define LUATZ_ZONEINFO   "/usr/share/zoneinfo/"  /* path where generic TZ files reside */
#define LUATZ_LOCALTIME  "localtime"             /* local time zone */
#define LUATZ_UTC        "UTC"                   /* UTC time zone */
#define LUATZ_METATABLE  "tz"                    /* TZ data metatable */
#define LUATZ_KEY        "TZ"                    /* TZ cache registry key */
#define LUATZ_EPOCH      2440588                 /* Julian day number of epoch (January 1, 1970) */
#define LUATZ_J0_TIME    -210866803200           /* Julian day 0 time (November 24, -4713) */
#define LUATZ_J0_YEAR    -4713                   /* Julian day 0 year (November 24, -4713) */


int luaopen_tz (lua_State *L);


#endif  /* _LUATZ_INCLUDED */
