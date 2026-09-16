#pragma once

#include <ctime>
#include <mutex>
#include <sstream>

class CTime32
{
public:
    CTime32() = delete;

    static __time32_t GetCurrentTime()
    {
        __time32_t result;
        _time32(&result);
        return result;
    }

    static __time32_t Make(int year, int month, int day, int hour, int min, int sec, int dst = -1)
    {
        return MakeUnixTime(year, month, day, hour, min, sec, dst);
    }

    static CString Format(__time32_t time)
    {
        auto *tm = _gmtime32(&time);
        if (tm == nullptr)
            return "n/a";

        static char buf[64] = { 0 };
        // TODO: Не умещается на экране загрузки сейвов :c
        // sprintf(buf, "%02d.%02d.%d %02d:%02d", tm->tm_mday, tm->tm_mon + 1,
        //        (tm->tm_year + 1900) % 100, tm->tm_hour, tm->tm_min);
        sprintf(buf, "%02d.%02d %02d:%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_hour, tm->tm_min);
        return buf;
    }

private:
    static std::mutex m_makeUnixTimeMutex;
    static __time32_t MakeUnixTime(tm &time)
    {
        std::lock_guard<std::mutex> guard(m_makeUnixTimeMutex);
        auto result = _mktime32(&time);
        return result + (_mktime32(_localtime32(&result)) - // NOLINT(concurrency-mt-unsafe)
                         _mktime32(_gmtime32(&result)));    // NOLINT(concurrency-mt-unsafe)
    }

    static __time32_t MakeUnixTime(int year, int month, int day, int hour, int min, int sec,
                                   int dst)
    {
        tm atm{};

        atm.tm_sec = sec;
        atm.tm_min = min;
        atm.tm_hour = hour;
        atm.tm_mday = day;
        atm.tm_mon = month - 1;
        atm.tm_year = year - 1900;
        atm.tm_isdst = dst;

        return _mktime32(&atm);
    }
};