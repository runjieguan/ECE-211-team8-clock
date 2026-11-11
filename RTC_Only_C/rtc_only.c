#include "rtc_io.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} RtcDateTime;

enum {
    RTC_ADDRESS = 0x68,
    RTC_CONTROL = 0x0E,
    RTC_STATUS = 0x0F,
    RTC_OWNED_CONTROL_BITS = 0xDF,
    RTC_OSF = 0x80
};

static bool rtc_ready;

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

static uint32_t seconds_since_2000(const RtcDateTime *value)
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

static bool rtc_date_time_valid(const RtcDateTime *value)
{
    if (value->year < 2000 || value->year > 2099 || value->month < 1 || value->month > 12 ||
        value->day < 1 || value->hour > 23 || value->minute > 59 || value->second > 59) {
        return false;
    }
    return value->day <= days_in_month(value->year, value->month);
}

static bool configure_square_wave(void)
{
    uint8_t control = 0;
    if (!hw_i2c_read(RTC_ADDRESS, RTC_CONTROL, &control, 1)) {
        return false;
    }
    /* Enable the oscillator and configure the 1 Hz SQW output. Preserve CONV. */
    control = (uint8_t)(control & ~RTC_OWNED_CONTROL_BITS);
    return hw_i2c_write(RTC_ADDRESS, RTC_CONTROL, &control, 1) &&
           hw_i2c_read(RTC_ADDRESS, RTC_CONTROL, &control, 1) &&
           (control & RTC_OWNED_CONTROL_BITS) == 0;
}

static bool rtc_get_date_time(RtcDateTime *value)
{
    uint8_t raw[7] = {0};
    if (!hw_i2c_read(RTC_ADDRESS, 0, raw, 7)) {
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

static bool rtc_set_date_time(const RtcDateTime *value)
{
    const uint32_t wanted = seconds_since_2000(value);
    const uint32_t days = wanted / 86400U;
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
    const uint32_t actual = seconds_since_2000(&readback);
    if (actual < wanted || actual - wanted > 2U) {
        return false;
    }

    uint8_t status = 0;
    if (!hw_i2c_read(RTC_ADDRESS, RTC_STATUS, &status, 1)) {
        return false;
    }
    /* Clear OSF after a verified write. Ones preserve the two alarm flags. */
    status = (uint8_t)((status & ~RTC_OSF) | 0x03);
    return hw_i2c_write(RTC_ADDRESS, RTC_STATUS, &status, 1) &&
           hw_i2c_read(RTC_ADDRESS, RTC_STATUS, &status, 1) && (status & RTC_OSF) == 0;
}

static unsigned read_pair(const char *digits)
{
    return (unsigned)((digits[0] - '0') * 10 + digits[1] - '0');
}

static bool parse_date_time(const char *text, RtcDateTime *time)
{
    const char pattern[] = "####-##-## ##:##:##";
    if (strlen(text) != sizeof(pattern) - 1) return false;
    for (size_t i = 0; i < sizeof(pattern) - 1; ++i) {
        if (pattern[i] == '#') {
            if (text[i] < '0' || text[i] > '9') return false;
        } else if (text[i] != pattern[i]) {
            return false;
        }
    }
    *time = (RtcDateTime){
        .year = (uint16_t)(100 * read_pair(text) + read_pair(text + 2)),
        .month = (uint8_t)read_pair(text + 5), .day = (uint8_t)read_pair(text + 8),
        .hour = (uint8_t)read_pair(text + 11), .minute = (uint8_t)read_pair(text + 14),
        .second = (uint8_t)read_pair(text + 17)
    };
    return rtc_date_time_valid(time);
}

static void print_time(void)
{
    RtcDateTime time;
    uint8_t status;
    if (!hw_i2c_read(RTC_ADDRESS, RTC_STATUS, &status, 1)) {
        hw_serial_write("RTC read failed. Check SDA and SCL.\n");
        return;
    }
    if (!rtc_get_date_time(&time)) {
        hw_serial_write("Time invalid or read failed. Check wiring before SET.\n");
        return;
    }
    char line[96];
    snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u  OSF=%u  ticks=%lu%s\n",
             (unsigned)time.year, (unsigned)time.month, (unsigned)time.day,
             (unsigned)time.hour, (unsigned)time.minute, (unsigned)time.second,
             (unsigned)((status & RTC_OSF) != 0), (unsigned long)hw_sqw_ticks(),
             (status & RTC_OSF) ? "  Use SET" : "");
    hw_serial_write(line);
}

static void command(const char *line)
{
    if (!rtc_ready) {
        hw_serial_write("RTC not ready. Check wiring and restart.\n");
    } else if (strcmp(line, "GET") == 0) {
        print_time();
    } else if (strncmp(line, "SET ", 4) == 0) {
        RtcDateTime time;
        if (!parse_date_time(line + 4, &time)) {
            hw_serial_write("Use SET YYYY-MM-DD HH:MM:SS (2000-2099).\n");
        } else if (!rtc_set_date_time(&time)) {
            hw_serial_write("RTC write or verification failed.\n");
        } else {
            print_time();
        }
    } else {
        hw_serial_write("Commands: GET or SET YYYY-MM-DD HH:MM:SS\n");
    }
}

void rtc_setup(void)
{
    rtc_ready = configure_square_wave();
    hw_serial_write(rtc_ready ? "DS3231 ready. SQW configured for 1 Hz. Send GET.\n"
                              : "DS3231 setup failed. Check 3V3, GND, SDA and SCL.\n");
}

void rtc_loop(void)
{
    static char line[32];
    static size_t length;
    const int ch = hw_serial_read();
    if (ch < 0) return;
    if (ch == '\n' || ch == '\r') {
        if (length == sizeof(line)) {
            hw_serial_write("Command too long.\n");
        } else if (length != 0) {
            line[length] = '\0';
            command(line);
        }
        length = 0;
    } else if (length < sizeof(line)) {
        if (length < sizeof(line) - 1) line[length] = (char)ch;
        ++length;
    }
}
