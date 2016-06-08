/*
 * Provides the Lua TZ module. See LICENSE for license terms.
 */

#ifndef LUATZ_INCLUDED
#define LUATZ_INCLUDED

#include <lua.h>

/**
 * Time component type. This must be at least 32-bit.
 */
#define LUATZ_COMPONENT int

/**
 * Local time TZ file.
 */
#define LUATZ_LOCALFILE "/etc/localtime"

/**
 * Directory where generic TZ files reside.
 */
#define LUATZ_ZONEINFO "/usr/share/zoneinfo/"

/**
 * Local time zone.
 */
#define LUATZ_LOCALTIME "localtime"

/**
 * UTC time zone.
 */
#define LUATZ_UTC "UTC"

/*
 * TZ data metatable.
 */
#define LUATZ_METATABLE "tz"

/**
 * TZ cache registry key.
 */
#define LUATZ_KEY "TZ"

/**
 * Julian day number of epoch (January 1, 1970).
 */
#define LUATZ_EPOCH 2440588

/**
 * Julian day 0 (November 24, -4713).
 */
#define LUATZ_J0_TIME -210866803200
#define LUATZ_J0_YEAR -4713

/**
 * Opens the TZ library in a Lua state.
 *
 * @param L the Lua state
 * @return the number of return values
 */
int luaopen_tz (lua_State *L);

#endif /* LUATZ_INCLUDED */
