# Lua TZ 


## Introduction

Lua TZ provides date and time functions with support for time zones. The core functions have an
interface similar to the standard functions `os.date` and `os.time`, but additionally accept a
time zone argument.


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

Pleaes see the [documentation](doc/) folder.


## Limitations

Lua TZ supports Lua 5.1, Lua 5.2, Lua 5.3, and Lua 5.4.

Lua TZ has been built and tested on Ubuntu Linux (64-bit) and MacOSX.

Lua TZ uses the tz database (also known as zoneinfo database) which must be installed on the host.

Lua TZ cannot process dates preceeding Julian day 0. Specifically, the minimum time processed by
Lua TZ is November 24, -4713 00:00:00 UTC in the proleptic Gregorian calendar using astronomical
year numbering. (The astronomical year -4713 corresponds to 4714 BC in the AD/BC numbering.)

Lua TZ ignores leap seconds.

Lua TZ ignores the TZ environment variable, and does not process TZ strings, standard/wall, and
the UTC/local indicators contained in timezone files.


## License

Lua TZ is released under the MIT license. See LICENSE for license terms.
