#ifndef TEAM_RTC_H
#define TEAM_RTC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} TeamRtcTime;

typedef enum {
    TEAM_RTC_OK,
    TEAM_RTC_IO_ERROR,
    TEAM_RTC_TIME_INVALID
} TeamRtcResult;

/* Wire access is supplied by the sketch. */
bool team_rtc_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, uint8_t count);
bool team_rtc_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data, uint8_t count);

/* Configure 1 Hz SQW through the control register. */
bool team_rtc_begin(void);
/* Return validated calendar time for 2000-2099 when the oscillator-stop flag is clear. */
TeamRtcResult team_rtc_read(TeamRtcTime *time);
/* Convert a validated 2000-2099 date and time to minutes since 2000-01-01. */
uint32_t team_rtc_calendar_minute(const TeamRtcTime *time);

#ifdef __cplusplus
}
#endif

#endif
