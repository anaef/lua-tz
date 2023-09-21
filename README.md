# Lua TZ 


## Introduction

Lua TZ provides date and time functions with support for time zones. The core functions have an
interface similar to the standard functions `os.date` and `os.time`, but additionally accept a
time zone argument.

Here are some quick examples:

```lua
local tz = require("tz")
 
-- Format a UTC timestamp in a time zone
local timestamp = os.time()
local date = tz.date("%Y-%m-%d %H:%M:%S", timestamp, "Europe/Zurich")
print(string.format("UTC %d in Zurich is %s", timestamp, date))

-- Convert a calendar time in a timezone to a UTC timestamp
local components = tz.date("*t", timestamp, "Europe/Zurich")
local timestamp_ny = tz.time(components, "America/New_York")
print(string.format("%s in New York is UTC %d", date, timestamp_ny))
```

Output:

```
UTC 1695364902 in Zurich is 2023-09-22 08:41:42
2023-09-22 08:41:42 in New York is UTC 1695386502
```


## Build, Test, and Install

### Building and Installing with LuaRocks

To build and install with LuaRocks, run:

```
luarocks install lua-tz
```


### Building, Testing and Installing with Make

Lua TZ comes with a simple Makefile. Please adapt the Makefile, and possibly tz.h, to your
environment, and then run:

```
make
make test
make install
```

## Release Notes

Please see the [release notes](NEWS.md) document.


## Documentation

Please see the [documentation](doc/) folder.


## Limitations

Lua TZ supports Lua 5.1, Lua 5.2, Lua 5.3, and Lua 5.4.

Lua TZ has been built and tested on Ubuntu Linux (64-bit) and MacOSX.

Lua TZ uses the tz database (also known as zoneinfo database) which must be installed on the host.

Lua TZ cannot process dates preceding Julian day 0. Specifically, the minimum time processed by
Lua TZ is November 24, -4713 00:00:00 UTC in the proleptic Gregorian calendar using astronomical
year numbering. (The astronomical year -4713 corresponds to 4714 BC in the AD/BC numbering.)

Lua TZ ignores leap seconds.

Lua TZ ignores the TZ environment variable, and does not process TZ strings, standard/wall, and
the UTC/local indicators contained in timezone files.


## License

Lua TZ is released under the MIT license. See LICENSE for license terms.
