#include "clock_logic.h"
#include "hardware.h"

enum {
    RTC_ADDRESS = 0x68,
    RTC_CONTROL = 0x0E,
    RTC_STATUS = 0x0F,
    RTC_OWNED_CONTROL_BITS = 0xDF
};

static const uint32_t seconds_per_minute = 60;
static const uint32_t seconds_per_day = 86400;
/* Exclusive end of the supported calendar, 2100-01-01 00:00:00. */
static const uint32_t end_of_2099 = UINT32_C(3155760000);
static bool rtc_initialized;

static uint8_t to_bcd(uint8_t value)
{
    return (uint8_t)((value / 10) * 16 + value % 10);
}

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
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (uint8_t)(lengths[month - 1] + (month == 2 && leap_year(year)));
}

uint32_t rtc_seconds_since_2000(const RtcDateTime *value)
{
    uint32_t days = 0;
    for (uint16_t year = 2000; year < value->year; ++year) {
        days += leap_year(year) ? 366U : 365U;
    }
    for (uint8_t month = 1; month < value->month; ++month) {
        days += days_in_month(value->year, month);
    }
    days += value->day - 1U;
    return ((days * 24U + value->hour) * 60U + value->minute) * 60U + value->second;
}

bool rtc_date_time_valid(const RtcDateTime *value)
{
    if (value->year < 2000 || value->year > 2099 || value->month < 1 || value->month > 12 ||
        value->day < 1 || value->hour > 23 || value->minute > 59 || value->second > 59) {
        return false;
    }
    return value->day <= days_in_month(value->year, value->month);
}

bool rtc_begin(void)
{
    uint8_t control = 0;
    if (rtc_initialized || !hw_i2c_read(RTC_ADDRESS, RTC_CONTROL, &control, 1)) {
        return false;
    }
    /* Enable the oscillator and configure the 1 Hz SQW output. Preserve CONV. */
    control = (uint8_t)(control & ~RTC_OWNED_CONTROL_BITS);
    if (!hw_i2c_write(RTC_ADDRESS, RTC_CONTROL, &control, 1) ||
        !hw_i2c_read(RTC_ADDRESS, RTC_CONTROL, &control, 1) ||
        (control & RTC_OWNED_CONTROL_BITS) != 0) {
        return false;
    }
    rtc_initialized = true;
    return true;
}

bool rtc_get_status(uint8_t *control, uint8_t *status)
{
    uint8_t registers[2] = {0};
    if (!rtc_initialized || !hw_i2c_read(RTC_ADDRESS, RTC_CONTROL, registers, 2)) {
        return false;
    }
    *control = registers[0];
    *status = registers[1];
    return true;
}

bool rtc_get_date_time(RtcDateTime *value)
{
    uint8_t raw[7] = {0};
    if (!rtc_initialized || !hw_i2c_read(RTC_ADDRESS, 0, raw, 7)) {
        return false;
    }
    /* Support 2000-2099 and reject reserved bits or an invalid weekday. */
    if ((raw[0] & 0x80) || (raw[1] & 0x80) || (raw[2] & 0x80) ||
        raw[3] < 1 || raw[3] > 7 || (raw[4] & 0xC0) || (raw[5] & 0xE0)) {
        return false;
    }

    RtcDateTime decoded = {0};
    uint8_t year = 0;
    uint8_t hour = 0;
    if (!from_bcd(raw[0], &decoded.second) || !from_bcd(raw[1], &decoded.minute) ||
        !from_bcd(raw[4], &decoded.day) || !from_bcd(raw[5], &decoded.month) ||
        !from_bcd(raw[6], &year)) {
        return false;
    }
    if (raw[2] & 0x40) {
        if (!from_bcd(raw[2] & 0x1F, &hour) || hour < 1 || hour > 12) {
            return false;
        }
        decoded.hour = (uint8_t)(hour % 12 + ((raw[2] & 0x20) ? 12 : 0));
    } else {
        if (!from_bcd(raw[2] & 0x3F, &hour)) {
            return false;
        }
        decoded.hour = hour;
    }
    decoded.year = (uint16_t)(2000U + year);
    if (!rtc_date_time_valid(&decoded)) {
        return false;
    }
    *value = decoded;
    return true;
}

bool rtc_set_date_time(const RtcDateTime *value)
{
    if (!rtc_initialized || !rtc_date_time_valid(value)) {
        return false;
    }
    const uint32_t wanted = rtc_seconds_since_2000(value);
    const uint32_t days = wanted / seconds_per_day;
    /* Sunday=1 ... Saturday=7. 2000-01-01 was Saturday. */
    const uint8_t weekday = (uint8_t)((days + 6U) % 7U + 1U);
    const uint8_t raw[7] = {
        to_bcd(value->second), to_bcd(value->minute), to_bcd(value->hour), weekday,
        to_bcd(value->day), to_bcd(value->month), to_bcd((uint8_t)(value->year - 2000))
    };
    if (!hw_i2c_write(RTC_ADDRESS, 0, raw, 7)) {
        return false;
    }

    RtcDateTime readback = {0};
    if (!rtc_get_date_time(&readback)) {
        return false;
    }
    const uint32_t actual = rtc_seconds_since_2000(&readback);
    if (actual < wanted || actual - wanted > 2U) {
        return false;
    }

    uint8_t status = 0;
    if (!hw_i2c_read(RTC_ADDRESS, RTC_STATUS, &status, 1)) {
        return false;
    }
    /* Clear OSF after a verified write. Ones preserve the two alarm flags. */
    status = (uint8_t)((status & 0x7F) | 0x03);
    if (!hw_i2c_write(RTC_ADDRESS, RTC_STATUS, &status, 1) ||
        !hw_i2c_read(RTC_ADDRESS, RTC_STATUS, &status, 1)) {
        return false;
    }
    return (status & 0x80) == 0;
}

void stopwatch_start(Stopwatch *stopwatch)
{
    if (!stopwatch->running) {
        stopwatch->started_at_us = hw_micros();
        stopwatch->running = true;
    }
}

void stopwatch_pause(Stopwatch *stopwatch)
{
    if (stopwatch->running) {
        stopwatch->accumulated_us += hw_micros() - stopwatch->started_at_us;
        stopwatch->running = false;
    }
}

void stopwatch_reset(Stopwatch *stopwatch)
{
    *stopwatch = (Stopwatch){0};
}

uint64_t stopwatch_elapsed_centiseconds(const Stopwatch *stopwatch)
{
    uint64_t elapsed = stopwatch->accumulated_us;
    if (stopwatch->running) elapsed += hw_micros() - stopwatch->started_at_us;
    return elapsed / UINT64_C(10000);
}

bool clock_alarm_arm(ClockAlarm *alarm, uint32_t now_seconds, uint8_t hour, uint8_t minute)
{
    if (now_seconds >= end_of_2099 || hour >= 24 || minute >= 60) {
        return false;
    }

    const uint32_t day_start = now_seconds - now_seconds % seconds_per_day;
    const uint32_t minute_of_day = (uint32_t)hour * 60 + minute;
    uint32_t due_seconds = day_start + minute_of_day * seconds_per_minute;
    if (due_seconds <= now_seconds) {
        due_seconds += seconds_per_day;
    }
    if (due_seconds >= end_of_2099) {
        return false;
    }

    alarm->hour = hour;
    alarm->minute = minute;
    alarm->due_seconds = due_seconds;
    alarm->armed = true;
    return true;
}

void clock_alarm_rebase(ClockAlarm *alarm, uint32_t now_seconds)
{
    if (alarm->armed && !clock_alarm_arm(alarm, now_seconds, alarm->hour, alarm->minute)) {
        clock_alarm_cancel(alarm);
    }
}

void clock_alarm_cancel(ClockAlarm *alarm)
{
    alarm->armed = false;
}

bool clock_alarm_take_due(ClockAlarm *alarm, uint32_t now_seconds)
{
    if (!alarm->armed || now_seconds < alarm->due_seconds || now_seconds >= end_of_2099) {
        return false;
    }
    clock_alarm_cancel(alarm);
    return now_seconds - alarm->due_seconds < seconds_per_minute;
}
