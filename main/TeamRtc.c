#include "TeamRtc.h"

enum {
    RTC_ADDRESS = 0x68,
    RTC_CONTROL = 0x0E,
    RTC_STATUS = 0x0F,
    RTC_OWNED_CONTROL_BITS = 0xDF,
    RTC_OSF = 0x80
};

static bool from_bcd(uint8_t bcd, uint8_t *value)
{
    if ((bcd & 0x0F) > 9 || (bcd >> 4) > 9) {
        return false;
    }
    *value = (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
    return true;
}

static bool leap_year(uint16_t year)
{
    /* Every fourth year in the supported range 2000-2099 is a leap year. */
    return year % 4U == 0;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (uint8_t)(lengths[month - 1] + (month == 2 && leap_year(year)));
}

static bool valid_calendar(const TeamRtcTime *time)
{
    if (time->month < 1 || time->month > 12 || time->day < 1 ||
        time->hour > 23 || time->minute > 59 || time->second > 59) {
        return false;
    }
    return time->day <= days_in_month(time->year, time->month);
}

bool team_rtc_begin(void)
{
    uint8_t control = 0;
    if (!team_rtc_i2c_read(RTC_ADDRESS, RTC_CONTROL, &control, 1)) {
        return false;
    }
    if ((control & RTC_OWNED_CONTROL_BITS) == 0) {
        return true;
    }
    /* Enable the oscillator and select 1 Hz SQW. Preserve the CONV bit. */
    control = (uint8_t)(control & ~RTC_OWNED_CONTROL_BITS);
    return team_rtc_i2c_write(RTC_ADDRESS, RTC_CONTROL, &control, 1) &&
           team_rtc_i2c_read(RTC_ADDRESS, RTC_CONTROL, &control, 1) &&
           (control & RTC_OWNED_CONTROL_BITS) == 0;
}

TeamRtcResult team_rtc_read(TeamRtcTime *time)
{
    uint8_t raw[7] = {0};
    uint8_t status = 0;
    if (!team_rtc_i2c_read(RTC_ADDRESS, 0, raw, 7) ||
        !team_rtc_i2c_read(RTC_ADDRESS, RTC_STATUS, &status, 1)) {
        return TEAM_RTC_IO_ERROR;
    }
    if ((status & RTC_OSF) || (raw[0] & 0x80) || (raw[1] & 0x80) || (raw[2] & 0x80) ||
        raw[3] < 1 || raw[3] > 7 || (raw[4] & 0xC0) || (raw[5] & 0xE0)) {
        return TEAM_RTC_TIME_INVALID;
    }

    TeamRtcTime decoded = {0};
    uint8_t year = 0;
    uint8_t hour = 0;
    if (!from_bcd(raw[0], &decoded.second) || !from_bcd(raw[1], &decoded.minute) ||
        !from_bcd(raw[4], &decoded.day) || !from_bcd(raw[5], &decoded.month) ||
        !from_bcd(raw[6], &year)) {
        return TEAM_RTC_TIME_INVALID;
    }
    if (raw[2] & 0x40) {
        if (!from_bcd(raw[2] & 0x1F, &hour) || hour < 1 || hour > 12) {
            return TEAM_RTC_TIME_INVALID;
        }
        decoded.hour = (uint8_t)(hour % 12 + ((raw[2] & 0x20) ? 12 : 0));
    } else {
        if (!from_bcd(raw[2] & 0x3F, &hour)) {
            return TEAM_RTC_TIME_INVALID;
        }
        decoded.hour = hour;
    }
    decoded.year = (uint16_t)(2000U + year);
    if (!valid_calendar(&decoded)) {
        return TEAM_RTC_TIME_INVALID;
    }
    *time = decoded;
    return TEAM_RTC_OK;
}

uint32_t team_rtc_calendar_minute(const TeamRtcTime *time)
{
    uint32_t days = 0;
    for (uint16_t year = 2000; year < time->year; ++year) {
        days += leap_year(year) ? 366U : 365U;
    }
    for (uint8_t month = 1; month < time->month; ++month) {
        days += days_in_month(time->year, month);
    }
    days += time->day - 1U;
    return (days * 24U + time->hour) * 60U + time->minute;
}
