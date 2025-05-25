local tz = require "tz"

-- ISO date format
local ISO = "%Y-%m-%dT%H:%M:%S"

-- Compare with standard functions
if os.getenv("TZ") then
	print("WARNING: TZ variable set; tests may fail")
end
local now = os.time()
assert(tz.date(nil, now) == os.date(nil, now))
assert(tz.date("%c", now) == os.date("%c", now))
assert(tz.date("!%c", now) == os.date("!%c", now))
assert(math.abs(tz.time() - os.time()) <= 1)
local t = os.date("*t", now)
assert(tz.time(t) == os.time(t))
local t1 = tz.date("*t", now)
for k, v in pairs(t) do
	assert(t1[k] == v, k)
end

-- Core functions (STD)
local now = 1392456870
local t = { tz.info(now, "Europe/Zurich") }
assert(t[1] == 3600)
assert(t[2] == false)
assert(t[3] == "CET")
local t = { tz.info(now, "America/New_York") }
assert(t[1] == -18000)
assert(t[2] == false)
assert(t[3] == "EST")
assert(tz.date(ISO, now, "Europe/Zurich") == "2014-02-15T10:34:30")
assert(tz.date(ISO, now, "America/New_York") == "2014-02-15T04:34:30")
local t = tz.date("*t", now, "Europe/Zurich")
assert(t.year == 2014)
assert(t.month == 2)
assert(t.day == 15)
assert(t.hour == 10)
assert(t.min == 34)
assert(t.sec == 30)
assert(t.wday == 7)
assert(t.yday == 46)
assert(t.isdst == false)
assert(t.off == 3600)
assert(t.zone == "CET")
assert(tz.time(t) == now)
local t = tz.date("*t", now, "America/New_York")
assert(t.year == 2014)
assert(t.month == 2)
assert(t.day == 15)
assert(t.hour == 4)
assert(t.min == 34)
assert(t.sec == 30)
assert(t.wday == 7)
assert(t.yday == 46)
assert(t.isdst == false)
assert(t.off == -18000)
assert(t.zone == "EST")
assert(tz.time(t) == now)

-- Core functions (DST)
local now = 1396173237
local t = { tz.info(now, "Europe/Zurich") }
assert(t[1] == 7200)
assert(t[2] == true)
assert(t[3] == "CEST")
local t = { tz.info(now, "America/New_York") }
assert(t[1] == -14400)
assert(t[2] == true)
assert(t[3] == "EDT")
assert(tz.date(ISO, now, "Europe/Zurich") == "2014-03-30T11:53:57")
assert(tz.date(ISO, now, "America/New_York") == "2014-03-30T05:53:57")
local t = tz.date("*t", now, "Europe/Zurich")
assert(t.year == 2014)
assert(t.month == 3)
assert(t.day == 30)
assert(t.hour == 11)
assert(t.min == 53)
assert(t.sec == 57)
assert(t.wday == 1)
assert(t.yday == 89)
assert(t.isdst == true)
assert(t.off == 7200)
assert(t.zone == "CEST")
assert(tz.time(t) == now)
local t = tz.date("*t", now, "America/New_York")
assert(t.year == 2014)
assert(t.month == 3)
assert(t.day == 30)
assert(t.hour == 5)
assert(t.min == 53)
assert(t.sec == 57)
assert(t.wday == 1)
assert(t.yday == 89)
assert(t.isdst == true)
assert(t.off == -14400)
assert(t.zone == "EDT")
assert(tz.time(t) == now)
assert(tz.time(t, "Europe/Zurich") ~= now)
t.off = nil
assert(tz.time(t, "America/New_York") == now)

-- Time change to DST
local t = { tz.info(1396141199, "Europe/Zurich") }
assert(t[1] == 3600)
assert(t[2] == false)
assert(t[3] == "CET")
local t = { tz.info(1396141200, "Europe/Zurich") }
assert(t[1] == 7200)
assert(t[2] == true)
assert(t[3] == "CEST")
assert(tz.date(ISO, 1396141199, "Europe/Zurich") == "2014-03-30T01:59:59")
assert(tz.date(ISO, 1396141200, "Europe/Zurich") == "2014-03-30T03:00:00")
local t = {
	year = 2014,
	month = 3,
	day = 30,
	hour = 1,
	min = 59,
	sec = 59
}
assert(tz.time(t, "Europe/Zurich") == 1396141199)
t.hour, t.min, t.sec = 2, 0, 0
assert(tz.time(t, "Europe/Zurich") == 1396141200)
t.hour = 3
assert(tz.time(t, "Europe/Zurich") == 1396141200)

-- Time change to STD
local t = { tz.info(1382835599, "Europe/Zurich") }
assert(t[1] == 7200)
assert(t[2] == true)
assert(t[3] == "CEST")
local t = { tz.info(1382835600, "Europe/Zurich") }
assert(t[1] == 3600)
assert(t[2] == false)
assert(t[3] == "CET")
assert(tz.date(ISO, 1382835599, "Europe/Zurich") == "2013-10-27T02:59:59")
assert(tz.date(ISO, 1382835600, "Europe/Zurich") == "2013-10-27T02:00:00")
local t = {
	year = 2013,
	month = 10,
	day = 27,
	hour = 1,
	min = 59,
	sec = 59
}
assert(tz.time(t, "Europe/Zurich") == 1382831999)
t.hour, t.min, t.sec = 2, 0, 0  -- 'hour 2b' default
assert(tz.time(t, "Europe/Zurich") == 1382835600) 
t.isdst = true  -- force 'hour 2a'
assert(tz.time(t, "Europe/Zurich") == 1382832000)
t.isdst = false  -- force 'hour 2b"
assert(tz.time(t, "Europe/Zurich") == 1382835600)
t.hour = 3
assert(tz.time(t, "Europe/Zurich") == 1382839200)

-- Dates requiring adjustment
assert(tz.time({ year = 2014, month = 1, day = 32 })
		== tz.time({ year = 2014, month = 2, day = 1 }))
assert(tz.time({ year = 2014, month = 2, day = 0 })
		== tz.time({ year = 2014, month = 1, day = 31 }))
assert(tz.time({ year = 2014, month = 1, day = 60 })
		== tz.time({ year = 2014, month = 3, day = 1 }))
assert(tz.time({ year = 2014, month = 2, day = -61 })
		== tz.time({ year = 2013, month = 12, day = 1 }))
assert(tz.time({ year = 2012, month = 2, day = 30 })
		== tz.time({ year = 2012, month = 3, day = 1 }))
assert(tz.time({ year = 2012, month = 3, day = 0 })
		== tz.time({ year = 2012, month = 2, day = 29 }))
assert(tz.time({ year = 2014, month = 1, day = 31, hour = 25, min = 61,	sec = 61 })
		== tz.time({ year = 2014, month = 2, day = 1, hour = 2,	min = 2, sec = 1 }))
assert(tz.time({ year = 2010, month = 1, day = 10 + 365 * 3 + 366 })
		== tz.time({ year = 2014, month = 1, day = 10 }))
assert(tz.time({ year = 2010, month = 1 + 49, day = 10 })
		== tz.time({ year = 2014, month = 2, day = 10 }))
assert(tz.time({ year = 2014, month = 1, day = 10 - 365 * 3 - 366 })
		== tz.time({ year = 2010, month = 1, day = 10 }))
assert(tz.time({ year = 2014, month = 1 - 47, day = 10 })
		== tz.time({ year = 2010, month = 2, day = 10 }))

-- Zero dates
assert(tz.time({ year = 1970, month = 1, day = 1, hour = 0, off = 0 }) == 0)
assert(tz.time({ year = 1970, month = 1, day = 1, hour = 0, sec = 1, off = 0 })	== 1)
assert(tz.time({ year = 1969, month = 12, day = 31, hour = 23, min = 59, sec = 59, off = 0 }) == -1)
assert(tz.date(ISO, 0, "UTC") == "1970-01-01T00:00:00")
assert(tz.date(ISO, 1, "UTC") == "1970-01-01T00:00:01")
assert(tz.date(ISO, -1, "UTC") == "1969-12-31T23:59:59")

-- Minimum dates
local t = { year = -4713, month = 11, day = 24, hour = 0, off = 0 }
assert(tz.time(t) == -210866803200)
t.sec = -1
assert(tz.time(t) == nil)
assert(tz.date(ISO, -210866803200, "UTC") == "-4713-11-24T00:00:00")
assert(tz.date(ISO, -210866803201, "UTC") == nil)
