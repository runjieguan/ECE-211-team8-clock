#ifndef CLOCK_LOGIC_H
#define CLOCK_LOGIC_H

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
} RtcDateTime;

bool rtc_begin(void);
bool rtc_get_date_time(RtcDateTime *value);
bool rtc_set_date_time(const RtcDateTime *value);
bool rtc_get_status(uint8_t *control, uint8_t *status);
bool rtc_date_time_valid(const RtcDateTime *value);
/* Convert a valid calendar value to seconds since 2000-01-01 00:00:00. */
uint32_t rtc_seconds_since_2000(const RtcDateTime *value);

/* Zero-initialize before first use. */
typedef struct {
    bool running;
    uint64_t accumulated_us;
    uint64_t started_at_us;
} Stopwatch;

void stopwatch_start(Stopwatch *stopwatch);
void stopwatch_pause(Stopwatch *stopwatch);
void stopwatch_reset(Stopwatch *stopwatch);
uint64_t stopwatch_elapsed_centiseconds(const Stopwatch *stopwatch);

/* Zero-initialize before first use. */
typedef struct {
    bool armed;
    uint8_t hour;
    uint8_t minute;
    uint32_t due_seconds;
} ClockAlarm;

/* An invalid setting leaves the current alarm intact. */
bool clock_alarm_arm(ClockAlarm *alarm, uint32_t now_seconds, uint8_t hour, uint8_t minute);
void clock_alarm_rebase(ClockAlarm *alarm, uint32_t now_seconds);
void clock_alarm_cancel(ClockAlarm *alarm);
/* Consume a due alarm once. An alarm missed by more than its minute expires. */
bool clock_alarm_take_due(ClockAlarm *alarm, uint32_t now_seconds);

#ifdef __cplusplus
}
#endif

#endif
