#include "clock_logic.h"
#include "hardware.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum Screen { MENU, CLOCK_FORMAT, CLOCK, STOPWATCH, SET_CLOCK, ALARM, SET_ALARM };
enum { LCD_WIDTH = 16, RTC_OSF = 0x80, RING_MS = 10000 };

static enum Screen screen = MENU;
static Stopwatch stopwatch;
static ClockAlarm alarm;
static bool rtc_ready, military_time, stream_clock;
static bool ringing, audio_playing;
static uint32_t ring_started_ms, last_alarm_check_ms;
static enum { AUDIO_SELECT_SD, AUDIO_SET_VOLUME, AUDIO_READY } audio_state;
static uint32_t audio_step_ms;
static uint32_t last_tick_count, last_tick_ms, last_warning_ms;
static uint32_t last_lcd_ms, last_key_poll_ms, key_changed_ms;
static char displayed[2][LCD_WIDTH + 1];
static char key_candidate = HW_KEY_NONE;
static bool key_released = true;

static const uint8_t date_positions[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
static const uint8_t alarm_positions[] = {0, 1, 3, 4};
static char entry[sizeof("____-__-__ __:__:__")];
static size_t entry_digits;
static char serial_line[64];
static size_t serial_length;
static bool serial_overflow;

static void consolef(const char *format, ...) {
    char text[192];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    hw_serial_write(text);
}

static void print_help(void) {
    hw_serial_write(
        "SET YYYY-MM-DD HH:MM:SS (2000-2099)\n"
        "GET | STATUS | STREAM ON | STREAM OFF | HELP\n"
        "FORMAT 24H | FORMAT MILITARY\n"
        "SW START | SW PAUSE | SW RESET | SW GET\n"
        "KEY <key> simulates a press, e.g. KEY #\n"
        "Alarm: C, A, four HHMM digits, A to save.\n");
}

static void lcd_lines(const char *first, const char *second) {
    const char *lines[] = {first, second};
    if (!hw_lcd_ready()) return;
    for (uint8_t row = 0; row < 2; ++row) {
        char padded[LCD_WIDTH + 1];
        snprintf(padded, sizeof(padded), "%-16.16s", lines[row]);
        if (strcmp(displayed[row], padded) != 0) {
            hw_lcd_row(row, padded);
            memcpy(displayed[row], padded, sizeof(padded));
        }
    }
}

static void format_clock(char *text, size_t size, const RtcDateTime *time) {
    if (military_time) {
        snprintf(text, size, "%02u%02u", (unsigned)time->hour, (unsigned)time->minute);
    } else {
        snprintf(text, size, "%02u:%02u:%02u", (unsigned)time->hour,
                 (unsigned)time->minute, (unsigned)time->second);
    }
}

static void format_stopwatch(char *text, size_t size, char separator) {
    const uint64_t cs = stopwatch_elapsed_centiseconds(&stopwatch);
    snprintf(text, size, "%02llu:%02u%c%02u", (unsigned long long)(cs / 6000),
             (unsigned)((cs / 100) % 60), separator, (unsigned)(cs % 100));
}

static void draw_lcd(void) {
    last_lcd_ms = hw_millis();
    if (!hw_lcd_ready()) return;
    char text[24];
    if (ringing) {
        snprintf(text, sizeof(text), "ALARM %02u:%02u", (unsigned)alarm.hour, (unsigned)alarm.minute);
        lcd_lines(text, "A:Stop  #:Menu");
        return;
    }

    switch (screen) {
        case MENU:
            if (hw_keypad_ready()) lcd_lines("A:Clock C:Alarm", "B:Stopwatch");
            else lcd_lines("Keypad not found", "Check SDA/SCL");
            break;
        case CLOCK_FORMAT:
            lcd_lines("Format    *:Set", "1:24h 0:Military");
            break;
        case CLOCK: {
            uint8_t control, status;
            RtcDateTime time;
            if (!rtc_ready || !rtc_get_status(&control, &status)) {
                lcd_lines("RTC read error", "#:Menu");
            } else if ((status & RTC_OSF) || !rtc_get_date_time(&time)) {
                lcd_lines("RTC needs setup", "*:Set    #:Menu");
            } else {
                format_clock(text, sizeof(text), &time);
                lcd_lines(text, "");
            }
            break;
        }
        case STOPWATCH:
            format_stopwatch(text, sizeof(text), ':');
            lcd_lines(text, "");
            break;
        case SET_CLOCK:
            if (entry_digits < 8) {
                snprintf(text, sizeof(text), "%.10s", entry);
                lcd_lines("Date YYYY-MM-DD", text);
            } else {
                lcd_lines(entry_digits == sizeof(date_positions) ? "A:Save B:Del" : "Time HH:MM:SS",
                          entry + 11);
            }
            break;
        case ALARM:
            if (alarm.armed) {
                snprintf(text, sizeof(text), "Alarm %02u:%02u ON", (unsigned)alarm.hour, (unsigned)alarm.minute);
                lcd_lines(text, "A:Set B:Off");
            } else {
                lcd_lines("Alarm OFF", "A:Set  #:Menu");
            }
            break;
        case SET_ALARM:
            snprintf(text, sizeof(text), "%.5s %s", entry,
                     entry_digits == sizeof(alarm_positions) ? "A:Save" : "B:Del");
            lcd_lines("Alarm time HHMM", text);
            break;
    }
}

static bool send_audio(uint8_t command, uint8_t value) {
    // DFPlayer frame uses feedback 1 and a 16-bit checksum.
    const uint16_t checksum = (uint16_t)-(0xFF + 0x06 + command + 1 + value);
    const uint8_t packet[] = {0x7E, 0xFF, 0x06, command, 1, 0, value,
                              (uint8_t)(checksum >> 8), (uint8_t)checksum, 0xEF};
    return hw_audio_write(packet, sizeof(packet));
}

static void update_audio(uint32_t now) {
    if (audio_state == AUDIO_SELECT_SD && now - audio_step_ms >= 1000 && send_audio(0x09, 2)) {
        audio_state = AUDIO_SET_VOLUME;
        audio_step_ms = now;
    } else if (audio_state == AUDIO_SET_VOLUME && now - audio_step_ms >= 500 && send_audio(0x06, 20)) {
        audio_state = AUDIO_READY;
    }
}

static void stop_ringing(void) {
    if (audio_playing) send_audio(0x16, 0);
    audio_playing = ringing = false;
}

static bool read_trusted_time(RtcDateTime *time) {
    uint8_t control, status;
    return rtc_ready && rtc_get_status(&control, &status) && !(status & RTC_OSF) && rtc_get_date_time(time);
}

static void update_alarm(uint32_t now, bool new_tick) {
    update_audio(now);
    if (ringing) {
        if (now - ring_started_ms >= RING_MS) {
            stop_ringing();
            draw_lcd();
        } else if (!audio_playing && audio_state == AUDIO_READY) {
            audio_playing = send_audio(0x03, 1);
        }
        return;
    }

    if (!alarm.armed || (!new_tick && now - last_alarm_check_ms < 1000)) return;
    last_alarm_check_ms = now;
    RtcDateTime time;
    if (!read_trusted_time(&time)) return;

    ringing = clock_alarm_take_due(&alarm, rtc_seconds_since_2000(&time));
    if (ringing) {
        ring_started_ms = hw_millis();
        audio_playing = audio_state == AUDIO_READY && send_audio(0x03, 1);
    }
    if (ringing || (!alarm.armed && screen == ALARM)) draw_lcd();
}

static bool is_digit(char key) {
    return key >= '0' && key <= '9';
}

static uint8_t read_pair(const char *text) {
    return (uint8_t)((text[0] - '0') * 10 + text[1] - '0');
}

static bool parse_date_time(const char *text, RtcDateTime *time) {
    if (strlen(text) != 19 || text[4] != '-' || text[7] != '-' || text[10] != ' ' ||
        text[13] != ':' || text[16] != ':') return false;
    for (size_t i = 0; i < sizeof(date_positions); ++i) {
        if (!is_digit(text[date_positions[i]])) return false;
    }
    *time = (RtcDateTime){
        .year = (uint16_t)(100 * read_pair(text) + read_pair(text + 2)),
        .month = read_pair(text + 5), .day = read_pair(text + 8),
        .hour = read_pair(text + 11), .minute = read_pair(text + 14), .second = read_pair(text + 17)
    };
    return rtc_date_time_valid(time);
}

static bool set_date_time(const RtcDateTime *time) {
    if (!rtc_set_date_time(time)) return false;
    if (alarm.armed) {
        RtcDateTime current = *time;
        rtc_get_date_time(&current);
        clock_alarm_rebase(&alarm, rtc_seconds_since_2000(&current));
    }
    return true;
}

static void edit_key(char key) {
    const bool editing_clock = screen == SET_CLOCK;
    const uint8_t *positions = editing_clock ? date_positions : alarm_positions;
    const size_t digits = editing_clock ? sizeof(date_positions) : sizeof(alarm_positions);
    if (is_digit(key) && entry_digits < digits) {
        entry[positions[entry_digits++]] = key;
    } else if (key == 'B' && entry_digits > 0) {
        entry[positions[--entry_digits]] = '_';
    } else if (key == 'A' && entry_digits == digits) {
        RtcDateTime time;
        if (editing_clock) {
            if (!parse_date_time(entry, &time)) {
                lcd_lines("Invalid entry", "B:Edit   #:Menu");
                return;
            }
            if (!set_date_time(&time)) {
                lcd_lines("RTC write error", "A:Retry  #:Menu");
                return;
            }
            screen = CLOCK_FORMAT;
        } else {
            const uint8_t hour = read_pair(entry);
            const uint8_t minute = read_pair(entry + 3);
            if (hour >= 24 || minute >= 60) {
                lcd_lines("Invalid HHMM", "B:Edit  #:Menu");
                return;
            }
            if (!read_trusted_time(&time)) {
                lcd_lines("RTC needs setup", "#:Menu then A *");
                return;
            }
            if (!clock_alarm_arm(&alarm, rtc_seconds_since_2000(&time), hour, minute)) {
                lcd_lines("Alarm date error", "B:Edit  #:Menu");
                return;
            }
            last_alarm_check_ms = hw_millis();
            screen = ALARM;
        }
    } else {
        return;
    }
    draw_lcd();
}

static void handle_key(char key) {
    if (ringing) {
        if (key != 'A' && key != '#') return;
        stop_ringing();
        if (key == '#') screen = MENU;
    } else if (key == '#') {
        screen = MENU;
    } else if (key == '*' && (screen == CLOCK || screen == CLOCK_FORMAT)) {
        if (!rtc_ready) {
            lcd_lines("RTC not found", "#:Menu");
            return;
        }
        strcpy(entry, "____-__-__ __:__:__");
        entry_digits = 0;
        screen = SET_CLOCK;
    } else if (screen == SET_CLOCK || screen == SET_ALARM) {
        edit_key(key);
        return;
    } else if (screen == MENU) {
        if (key == 'A') screen = CLOCK_FORMAT;
        else if (key == 'B') screen = STOPWATCH;
        else if (key == 'C') screen = ALARM;
        else return;
    } else if (screen == CLOCK_FORMAT && (key == '0' || key == '1')) {
        military_time = key == '0';
        screen = CLOCK;
    } else if (screen == STOPWATCH && key == 'A') {
        if (stopwatch.running) stopwatch_pause(&stopwatch);
        else stopwatch_start(&stopwatch);
    } else if (screen == STOPWATCH && key == 'B') {
        stopwatch_reset(&stopwatch);
    } else if (screen == ALARM && key == 'A') {
        strcpy(entry, "__:__");
        entry_digits = 0;
        screen = SET_ALARM;
    } else if (screen == ALARM && key == 'B') {
        clock_alarm_cancel(&alarm);
    } else {
        return;
    }
    draw_lcd();
}

static void poll_keypad(void) {
    const uint32_t now = hw_millis();
    if (!hw_keypad_ready() || now - last_key_poll_ms < 10) return;
    last_key_poll_ms = now;
    const char key = hw_key();
    if (key != key_candidate) {
        key_candidate = key;
        key_changed_ms = now;
    }
    if (key == HW_KEY_ERROR || now - key_changed_ms < 25) return;
    if (key == HW_KEY_NONE) {
        key_released = true;
    } else if (key_released) {
        key_released = false;
        handle_key(key);
    }
}

static void print_status(void) {
    uint8_t control, status;
    if (!rtc_get_status(&control, &status)) {
        hw_serial_write("STATUS: I2C read failed.\n");
        return;
    }
    RtcDateTime time;
    const uint32_t ticks = hw_rtc_ticks();
    consolef("STATUS: I2C=OK control=0x%02X OSF=%u calendar=%s SQW_1Hz=%s ticks=%lu recent_tick=%s\n",
             (unsigned)control, (unsigned)((status & RTC_OSF) != 0),
             rtc_get_date_time(&time) ? "VALID" : "INVALID_OR_READ_ERROR",
             (control & 0x1C) == 0 ? "YES" : "NO", (unsigned long)ticks,
             ticks != 0 && hw_millis() - last_tick_ms < 3000 ? "YES" : "NO");
}

static void print_time(void) {
    uint8_t control, status;
    RtcDateTime time;
    if (!rtc_get_status(&control, &status)) {
        hw_serial_write("ERROR: RTC status read failed.\n");
    } else if (!rtc_get_date_time(&time)) {
        hw_serial_write("TIME INVALID OR READ FAILED. Check wiring, then use SET.\n");
    } else {
        char text[12];
        format_clock(text, sizeof(text), &time);
        consolef("ticks=%lu time=%04u-%02u-%02u %s trusted=%s\n", (unsigned long)hw_rtc_ticks(),
                 (unsigned)time.year, (unsigned)time.month, (unsigned)time.day, text,
                 (status & RTC_OSF) ? "NO (send SET)" : "YES");
    }
}

static bool stopwatch_command(const char *command) {
    if (strcmp(command, "SW START") == 0) {
        stopwatch_start(&stopwatch);
        screen = STOPWATCH;
    } else if (strcmp(command, "SW PAUSE") == 0) {
        stopwatch_pause(&stopwatch);
    } else if (strcmp(command, "SW RESET") == 0) {
        stopwatch_reset(&stopwatch);
    } else if (strcmp(command, "SW GET") != 0) {
        return false;
    }
    char text[24];
    format_stopwatch(text, sizeof(text), '.');
    consolef("SW %s %s\n", stopwatch.running ? "RUNNING" : "PAUSED", text);
    if (screen == STOPWATCH) draw_lcd();
    return true;
}

static void handle_command(const char *command) {
    if (strcmp(command, "HELP") == 0) {
        print_help();
    } else if (strncmp(command, "KEY ", 4) == 0) {
        if (strlen(command) == 5 && strchr("0123456789ABCD#*", command[4])) handle_key(command[4]);
        else hw_serial_write("ERROR: Send one keypad character, e.g. KEY A.\n");
    } else if (stopwatch_command(command)) {
        return;
    } else if (!rtc_ready) {
        hw_serial_write("RTC not ready. Disconnect USB before checking wiring, then reset.\n");
    } else if (strcmp(command, "GET") == 0) {
        print_time();
    } else if (strcmp(command, "STATUS") == 0) {
        print_status();
    } else if (strcmp(command, "STREAM ON") == 0) {
        stream_clock = true;
        screen = CLOCK;
        hw_serial_write("Time stream ON.\n");
        draw_lcd();
    } else if (strcmp(command, "STREAM OFF") == 0) {
        stream_clock = false;
        hw_serial_write("Time stream OFF. Tick counting continues.\n");
    } else if (strncmp(command, "FORMAT ", 7) == 0) {
        if (strcmp(command + 7, "24H") == 0) military_time = false;
        else if (strcmp(command + 7, "MILITARY") == 0) military_time = true;
        else {
            hw_serial_write("ERROR: Use FORMAT 24H or FORMAT MILITARY.\n");
            return;
        }
        print_time();
        if (screen == CLOCK) draw_lcd();
    } else if (strncmp(command, "SET ", 4) == 0) {
        RtcDateTime time;
        if (!parse_date_time(command + 4, &time)) {
            hw_serial_write("ERROR: Use SET YYYY-MM-DD HH:MM:SS; valid date, years 2000-2099.\n");
        } else if (!set_date_time(&time)) {
            hw_serial_write("ERROR: Time write or verification failed. Send STATUS and GET.\n");
        } else {
            hw_serial_write("SET OK. Calendar verified, OSF cleared. Tick counter was not reset.\n");
            print_time();
            if (screen == CLOCK) draw_lcd();
        }
    } else {
        hw_serial_write("Unknown command. Send HELP. Commands are uppercase.\n");
    }
}

static void read_commands(void) {
    // Keep input work bounded so the alarm and keypad can run each loop.
    for (unsigned budget = 0; budget < sizeof(serial_line) && hw_serial_available() > 0; ++budget) {
        const int key = hw_serial_read();
        if (key < 0) break;
        if (key == '\r' || key == '\n') {
            if (serial_overflow) {
                hw_serial_write("ERROR: Command too long. Send the complete command again.\n");
            } else if (serial_length != 0) {
                serial_line[serial_length] = '\0';
                handle_command(serial_line);
            }
            serial_length = 0;
            serial_overflow = false;
        } else if (serial_length < sizeof(serial_line) - 1) {
            serial_line[serial_length++] = (char)key;
        } else {
            serial_overflow = true;
        }
    }
}

void app_init(void) {
    rtc_ready = rtc_begin();
    audio_step_ms = last_warning_ms = last_tick_ms = hw_millis();
    draw_lcd();
    if (rtc_ready) {
        hw_serial_write("RTC READY: address 0x68; SQW set to 1 Hz.\n");
        print_status();
        print_time();
    } else {
        hw_serial_write("RTC INIT FAILED. Disconnect USB and check 3V3/GND/SDA/SCL.\n");
    }
    hw_serial_write(hw_keypad_ready() ? "KEYPAD READY: address 0x20.\n" : "Keypad not found at 0x20.\n");
    hw_serial_write(hw_lcd_ready() ? "LCD READY: address 0x27.\n" : "LCD not found at 0x27.\n");
    print_help();
}

void app_loop(void) {
    read_commands();
    poll_keypad();
    const uint32_t now = hw_millis();
    const uint32_t ticks = hw_rtc_ticks();
    const bool new_tick = rtc_ready && ticks != last_tick_count;
    if (new_tick) {
        last_tick_count = ticks;
        last_tick_ms = now;
        if (stream_clock && screen == CLOCK) print_time();
    }
    if (rtc_ready && now - last_tick_ms >= 5000 && now - last_warning_ms >= 5000) {
        hw_serial_write("NO SQW TICKS FOR 5 s. Check the SQW pin and its pull-up. GET uses I2C.\n");
        last_warning_ms = now;
    }
    update_alarm(now, new_tick);
    const uint32_t since_refresh = hw_millis() - last_lcd_ms;
    if (!ringing && ((screen == CLOCK && (new_tick || since_refresh >= 1000)) ||
                     (screen == STOPWATCH && stopwatch.running && since_refresh >= 100))) {
        draw_lcd();
    }
}
