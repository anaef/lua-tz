# Lua TZ README

## Introduction

Lua TZ provides date and time function with support of time zones. Where
applicable, the functions are similar to the standard functions `os.date`
and `os.time`.


## Build, Test, and Install

Lua TZ comes with a simple Makefile. Please adapt the Makefile, and possibly
tz.h, to your environment, and then run:

```
  make <platform>
  make test
  make install
```

## Reference

### Types

#### `timezone`

A timezone value corresponds to a IANA Time Zone Database name. This typically
is a string of the format "area/location", e.g. "Europe/Zurich". Generally, any
timezone file installed in the zoneinfo directory of the host can be specified,
including miscellaneous time zones, such as "UTC". The special string
"localtime" represents the local time zone of the host.


#### `time`

A time value corresponds to the number of seconds since the epoch, ignoring
leap seconds. The Lua TZ epoch is January 1, 1970 00:00:00 UTC. Time values are
internally represented as 64-bit signed integers. In Lua 5.1 and 5.2, they
are represented as numbers (`lua_Number`).


### Functions

#### `tz.type ([time [, timezone]])`

Returns the offset from UTC in seconds, a boolean indicating whether daylight
saving time is on, and an abbreviated time zone name.

If the time argument is present, this is the time to return information for.
Otherwise, information is returned for the current time.

If the timezone argument is present, this is the time zone to return information
for. Otherwise, information is returned for the local time zone of the host.


#### `tz.date ([format [, time [, timezone]]])`

This function behaves similar to `os.date`, but additionally accepts a time
zone.

If the timezone argument is present, this is the time zone to format the date
in. Otherwise, the date is formatted in the local time zone of the host.

If the format argument starts with "!", then the date is formated in UTC
regardless of the timezone argument. After this optional character, if the
format argument is the string "\*t", then the function returns a table similar
to `os.date`, but with the following additional fields:

* `off` (offset from UTC, in seconds)
* `zone` (abbreviated time zone name)


#### `tz.time ([table [, timezone])`

This function behaves similar to `os.time`, but additionally accepts a time
zone.

If the timezone argument is present, this is the time zone of the date
represented by the table. Otherwise, the date is assumed to be in the local
time zone of the host.

If the table argument contains a field `off`, this offset from UTC in seconds
is applied to the date regardless of the timezone argument.


## Limitations

Lua TZ supports Lua 5.1 and Lua 5.2.

Lua TZ has been built and tested on Ubuntu Linux (64-bit) and MacOSX.

Lua TZ uses the tz database (also known as zoneinfo database) which must be
installed on the host.

Lua TZ cannot process dates preceeding Julian day 0. Specifically, the
minimum time processed by Lua TZ is November 24, -4713 00:00:00 UTC in the
proleptic Gregorian calendar (using astronomical year numbering; the
astronomical year -4713 corresponds to 4714 BC in the AD/BC numbering.)

Lua TZ ignores leap seconds.

Lua TZ ignores the TZ environment variable, and does not process TZ strings,
standard/wall, and the UTC/local indicators contained in timezone files.


## License

Lua TZ is released under the MIT license. See LICENSE for license terms.
