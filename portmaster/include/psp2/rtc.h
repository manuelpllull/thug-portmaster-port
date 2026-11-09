#ifndef _PSP2_RTC_H_
#define _PSP2_RTC_H_

#include <psp2/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SceDateTime {
    unsigned short year;
    unsigned short month;
    unsigned short day;
    unsigned short hour;
    unsigned short minute;
    unsigned short second;
    unsigned int microsecond;
} SceDateTime;

static inline int sceRtcGetCurrentClockLocalTime(SceDateTime *time_val)
{
    time_t rawtime;
    time(&rawtime);
    struct tm *ti = localtime(&rawtime);
    if (!time_val || !ti) return -1;
    time_val->year = ti->tm_year + 1900;
    time_val->month = ti->tm_mon + 1;
    time_val->day = ti->tm_mday;
    time_val->hour = ti->tm_hour;
    time_val->minute = ti->tm_min;
    time_val->second = ti->tm_sec;
    time_val->microsecond = 0;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // _PSP2_RTC_H_
