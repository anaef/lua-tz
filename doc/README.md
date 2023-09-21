# Lua TZ Documentation

This page described the values used and functions provided by Lua TZ.


## Values

### `timezone`

A timezone value is a string that corresponds to a IANA Time Zone Database name. Its typical
format is `"area/location"`, e.g., `"Europe/Zurich"`. Generally, any timezone file installed in
the zoneinfo directory of the host can be specified, including miscellaneous time zones, such as
`"UTC"`. The special name `"localtime"` represents the local time zone of the host.


### `time`

A time value corresponds to the number of seconds since the epoch, ignoring leap seconds. The
Lua TZ epoch is January 1, 1970 00:00:00 UTC. Lua TZ internally processes time values as 64-bit
signed integers. As of Lua 5.3, the Lua representation is an integer; in prior versions of Lua, it
is a number.


## Functions

### `tz.info ([time [, timezone]])`

Returns three values pertinent to the specified time and timezone: the offset from UTC in seconds,
a boolean indicating whether daylight saving time is on, and an abbreviated time zone name.

If the `time` argument is not present, the information is returned for the current time.

If the `timezone` argument is not present, the information is returned for the local time zone of
the host.


### `tz.date ([format [, time [, timezone]]])`

The function behaves similar to `os.date`, but additionally accepts a time zone.

If the `timezone` argument is present, the date is formatted in that time zone. Otherwise, the
time zone defaults to the local time zone of the host.

If the `format` argument starts with `'!'`, then the date is formated in UTC regardless of the
`timezone` argument. After this optional character, if the format argument is the string `"*t"`,
then the function returns a table similar to `os.date`, but with the following additional fields:

* `off` (offset from UTC, in seconds)
* `zone` (abbreviated time zone name)


### `tz.time ([table [, timezone]])`

The function behaves similar to `os.time`, but additionally accepts a time zone.

If the `timezone` argument is present, the calendar date and time values specified by the table
are processed in that time zone. Else, if the table contains an `off` field, the values are
processed with the given offset from UTC in seconds. Otherwise, the values are processed in the
local time zone of the host.
