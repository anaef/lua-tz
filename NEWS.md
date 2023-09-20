# Lua TZ Release Notes


## Release 1.0.0 (2023-09-20)

- Improved support for Lua 5.3+ integers.

- The `tz.type` function is now called `tz.info`. Its old name has been deprecated.

- The `tz.time` function incorrectly forced a match of the `isdst` field. The field is now
only used to select 'hour a' or 'hour b' when GMT offsets decrease, i.e., when going from daylight
saving time to standard time, and an 'hour' occurs twice.

- The `tz.time` function now gives precedence to the `timezone` argument over an `off` field. If
`timezone` is not present, the function honors the `off` field as before.

> [!IMPORTANT]
> Together, the two changes to `tz.time` mean that it is often no longer necessary to clear the
> `off` and `isdst` fields when converting calendar date and time among time zones, thus
> eliminating a source of error. However, if your code relies on the `off` field taking precedence
> over an explicitly provided time zone, it must be updated.


## Release 0.9.0 (2014-04-06)

- Initial public release.
