# Lua TZ 


## Introduction

Lua TZ provides date and time function with support of time zones. Where applicable, the functions
are similar to the standard functions `os.date` and `os.time`.


## Build, Test, and Install

Lua TZ comes with a simple Makefile. Please adapt the Makefile, and possibly tz.h, to your
environment, and then run:

```
  make
  make test
  make install
```

## Documentation

Pleaes see the [documentation](doc/) folder.


## Limitations

Lua TZ supports Lua 5.1, Lua 5.2, and Lua 5.3.

Lua TZ has been built and tested on Ubuntu Linux (64-bit) and MacOSX.

Lua TZ uses the tz database (also known as zoneinfo database) which must be installed on the host.

Lua TZ cannot process dates preceeding Julian day 0. Specifically, the minimum time processed by
Lua TZ is November 24, -4713 00:00:00 UTC in the proleptic Gregorian calendar (using astronomical
year numbering; the astronomical year -4713 corresponds to 4714 BC in the AD/BC numbering.)

Lua TZ ignores leap seconds.

Lua TZ ignores the TZ environment variable, and does not process TZ strings, standard/wall, and
the UTC/local indicators contained in timezone files.


## License

Lua TZ is released under the MIT license. See LICENSE for license terms.
