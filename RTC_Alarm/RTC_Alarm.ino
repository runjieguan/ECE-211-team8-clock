#include <Arduino.h>
#include <Wire.h>
#include <I2CKeyPad.h>
#include <LiquidCrystal_I2C.h>
#include <stdio.h>
#include <string.h>
#include "AlarmAudio.h"
#include "ClockAlarm.h"
#include "RtcClock.h"
#include "Stopwatch.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr uint8_t SDA_PIN = 4;
constexpr uint8_t SCL_PIN = 5;
constexpr uint8_t SQW_PIN = 6;
#elif defined(CONFIG_IDF_TARGET_ESP32)
// Select Adafruit ESP32 Feather. GPIO14 is the existing RTC_LCD SQW pin.
constexpr uint8_t SDA_PIN = SDA;
constexpr uint8_t SCL_PIN = SCL;
constexpr uint8_t SQW_PIN = 14;
#else
#error "Select an ESP32-S3 or Adafruit ESP32 Feather board."
#endif

constexpr uint8_t KEYPAD_ADDRESS = 0x20;
constexpr char KEYPAD_MAP[] = "D#0*C987B654A321";  // Team's heavy-duty keypad.
constexpr uint32_t KEYPAD_POLL_MS = 10;
constexpr uint32_t KEYPAD_STABLE_MS = 25;

constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;
constexpr uint32_t ALARM_RING_MS = 10000;

enum class Screen { MainMenu, ClockFormat, Clock, Stopwatch, SetClock, AlarmMenu, SetAlarm };
Screen screen = Screen::MainMenu;
I2CKeyPad keypad(KEYPAD_ADDRESS);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
bool keypadReady = false;
bool lcdReady = false;
char displayedRows[LCD_ROWS][LCD_COLUMNS + 1] = {};
uint32_t lastLcdRefreshMs = 0;
uint32_t lastKeyPollMs = 0;
uint32_t keyCandidateSinceMs = 0;
uint8_t keyCandidate = I2C_KEYPAD_NOKEY;
bool keyArmed = true;

Stopwatch stopwatch;
ClockAlarm clockAlarm;
char alarmDigits[4] = {};
uint8_t alarmDigitsEntered = 0;
bool alarmRinging = false;
bool alarmAudioStarted = false;
uint32_t alarmRingStartedMs = 0;
uint32_t lastAlarmCheckMs = 0;

constexpr char CLOCK_ENTRY_TEMPLATE[] = "____-__-__ __:__:__";
constexpr uint8_t CLOCK_DIGIT_POSITIONS[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
constexpr uint8_t CLOCK_DIGIT_COUNT = sizeof(CLOCK_DIGIT_POSITIONS);
char clockEntry[sizeof(CLOCK_ENTRY_TEMPLATE)] = {};
uint8_t clockDigitsEntered = 0;

bool rtcReady = false;
bool clockStream = false;  // Optional serial output. The LCD refreshes independently.
bool militaryTimeDisplay = false;
uint32_t lastTicks = 0;
uint32_t lastTickMs = 0;
uint32_t lastWarningMs = 0;
char commandBuffer[64] = {};
size_t commandLength = 0;
bool commandOverflow = false;

void printHelp() {
  Serial.println("Commands (24-hour time, years 2000-2099):");
  Serial.println("SET YYYY-MM-DD HH:MM:SS");
  Serial.println("GET | STATUS | STREAM ON | STREAM OFF | HELP");
  Serial.println("FORMAT 24H (08:00:00) | FORMAT MILITARY (0800)");
  Serial.println("SW START | SW PAUSE | SW RESET | SW GET");
  Serial.println("KEY <key> simulates one keypad press, e.g. KEY #");
  Serial.println("Alarm: KEY C, KEY A, four HHMM digits, KEY A to save.");
  Serial.println("Stopwatch display: MM:SS:cc. LCD updates about every 100 ms while running.");
  Serial.println("SET changes RTC date/time and SQW phase; stopwatch uses ESP32 elapsed time.");
}

void printStopwatch() {
  const uint64_t cs = stopwatch.elapsedCentiseconds();
  const uint64_t minutes = cs / 6000ULL;
  const unsigned int seconds = static_cast<unsigned int>((cs / 100ULL) % 60ULL);
  const unsigned int hundredths = static_cast<unsigned int>(cs % 100ULL);
  Serial.printf("SW %s %02llu:%02u.%02u\n",
                stopwatch.isRunning() ? "RUNNING" : "PAUSED",
                static_cast<unsigned long long>(minutes), seconds, hundredths);
}

void showLcd(const char *firstRow, const char *secondRow) {
  if (!lcdReady) {
    return;
  }
  const char *rows[LCD_ROWS] = {firstRow, secondRow};
  for (uint8_t row = 0; row < LCD_ROWS; ++row) {
    char padded[LCD_COLUMNS + 1];
    snprintf(padded, sizeof(padded), "%-16.16s", rows[row]);
    if (strcmp(displayedRows[row], padded) != 0) {
      lcd.setCursor(0, row);
      lcd.print(padded);
      memcpy(displayedRows[row], padded, sizeof(padded));
    }
  }
}

void drawLcd() {
  lastLcdRefreshMs = millis();
  if (!lcdReady) {
    return;
  }
  if (alarmRinging) {
    char text[17];
    snprintf(text, sizeof(text), "ALARM %02u:%02u", static_cast<unsigned int>(clockAlarm.hour()),
             static_cast<unsigned int>(clockAlarm.minute()));
    showLcd(text, "A:Stop  #:Menu");
    return;
  }
  switch (screen) {
    case Screen::MainMenu:
      if (keypadReady) {
        showLcd("A:Clock C:Alarm", "B:Stopwatch");
      } else {
        showLcd("Keypad not found", "Check SDA/SCL");
      }
      break;
    case Screen::ClockFormat:
      showLcd("Format    *:Set", "1:24h 0:Military");
      break;
    case Screen::Clock: {
      uint8_t control = 0;
      uint8_t status = 0;
      RtcDateTime value = {};
      if (!rtcReady || !rtcGetStatus(control, status)) {
        showLcd("RTC read error", "#:Menu");
        break;
      }
      if ((status & 0x80) != 0 || !rtcGetDateTime(value)) {
        showLcd("RTC needs setup", "*:Set    #:Menu");
        break;
      }
      const unsigned int hour = value.hour;
      const unsigned int minute = value.minute;
      char text[9];
      if (militaryTimeDisplay) {
        snprintf(text, sizeof(text), "%02u%02u", hour, minute);
      } else {
        snprintf(text, sizeof(text), "%02u:%02u:%02u", hour, minute,
                 static_cast<unsigned int>(value.second));
      }
      showLcd(text, "");
      break;
    }
    case Screen::Stopwatch: {
      const uint64_t centiseconds = stopwatch.elapsedCentiseconds();
      const uint64_t minutes = centiseconds / 6000ULL;
      const unsigned int seconds = static_cast<unsigned int>((centiseconds / 100ULL) % 60ULL);
      const unsigned int hundredths = static_cast<unsigned int>(centiseconds % 100ULL);
      char text[24];
      snprintf(text, sizeof(text), "%02llu:%02u:%02u",
               static_cast<unsigned long long>(minutes), seconds, hundredths);
      showLcd(text, "");
      break;
    }
    case Screen::SetClock:
      if (clockDigitsEntered < 8) {
        char date[11];
        memcpy(date, clockEntry, sizeof(date) - 1);
        date[sizeof(date) - 1] = '\0';
        showLcd("Date YYYY-MM-DD", date);
      } else if (clockDigitsEntered < CLOCK_DIGIT_COUNT) {
        showLcd("Time HH:MM:SS", clockEntry + 11);
      } else {
        showLcd("A:Save B:Del", clockEntry + 11);
      }
      break;
    case Screen::AlarmMenu: {
      char text[17];
      if (clockAlarm.isArmed()) {
        snprintf(text, sizeof(text), "Alarm %02u:%02u ON", static_cast<unsigned int>(clockAlarm.hour()),
                 static_cast<unsigned int>(clockAlarm.minute()));
        showLcd(text, "A:Set B:Off");
      } else {
        showLcd("Alarm OFF", "A:Set  #:Menu");
      }
      break;
    }
    case Screen::SetAlarm: {
      char text[17];
      snprintf(text, sizeof(text), "%c%c:%c%c %s", alarmDigits[0], alarmDigits[1],
               alarmDigits[2], alarmDigits[3], alarmDigitsEntered == sizeof(alarmDigits) ? "A:Save" : "B:Del");
      showLcd("Alarm time HHMM", text);
      break;
    }
  }
}

bool readTrustedRtcTime(RtcDateTime &value) {
  uint8_t control = 0;
  uint8_t status = 0;
  return rtcReady && rtcGetStatus(control, status) && (status & 0x80) == 0 && rtcGetDateTime(value);
}

void rebaseAlarmAfterClockSet(const RtcDateTime &setValue) {
  if (clockAlarm.isArmed()) {
    RtcDateTime current = setValue;
    rtcGetDateTime(current);
    clockAlarm.rebase(rtcSecondsSince2000(current));
  }
}

void stopAlarmRing() {
  if (alarmAudioStarted) {
    alarmAudioStop();
  }
  alarmAudioStarted = false;
  alarmRinging = false;
}

void updateAlarm(bool newRtcTick) {
  const uint32_t now = millis();
  alarmAudioUpdate(now);
  if (alarmRinging) {
    if (now - alarmRingStartedMs >= ALARM_RING_MS) {
      stopAlarmRing();
      drawLcd();
    } else if (!alarmAudioStarted) {
      alarmAudioStarted = alarmAudioPlay();
    }
    return;
  }

  if (!clockAlarm.isArmed() || (!newRtcTick && now - lastAlarmCheckMs < 1000U)) {
    return;
  }
  lastAlarmCheckMs = now;
  RtcDateTime current = {};
  if (!readTrustedRtcTime(current)) {
    return;
  }
  if (clockAlarm.takeDue(rtcSecondsSince2000(current))) {
    alarmRinging = true;
    alarmRingStartedMs = millis();
    alarmAudioStarted = alarmAudioPlay();
    drawLcd();
  } else if (!clockAlarm.isArmed() && screen == Screen::AlarmMenu) {
    drawLcd();
  }
}

bool handleStopwatchCommand(const char *command) {
  if (strcmp(command, "SW START") == 0) {
    stopwatch.start();
    screen = Screen::Stopwatch;
  } else if (strcmp(command, "SW PAUSE") == 0) {
    stopwatch.pause();
  } else if (strcmp(command, "SW RESET") == 0) {
    stopwatch.reset();
  } else if (strcmp(command, "SW GET") != 0) {
    return false;
  }
  printStopwatch();
  if (screen == Screen::Stopwatch) {
    drawLcd();
  }
  return true;
}

void printCurrentTime() {
  uint8_t control = 0;
  uint8_t status = 0;
  if (!rtcGetStatus(control, status)) {
    Serial.println("ERROR: RTC status read failed.");
    return;
  }
  RtcDateTime value = {};
  if (!rtcGetDateTime(value)) {
    Serial.println("TIME INVALID OR READ FAILED. Check wiring; use SET after communication is OK.");
    return;
  }
  const unsigned int hour = value.hour;
  const unsigned int minute = value.minute;
  char timeText[9];
  if (militaryTimeDisplay) {
    snprintf(timeText, sizeof(timeText), "%02u%02u", hour, minute);
  } else {
    snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u", hour, minute,
             static_cast<unsigned int>(value.second));
  }
  Serial.printf(
      "ticks=%lu time=%04u-%02u-%02u %s trusted=%s\n",
      static_cast<unsigned long>(getRtcTicks()),
      static_cast<unsigned int>(value.year), static_cast<unsigned int>(value.month),
      static_cast<unsigned int>(value.day), timeText,
      (status & 0x80) ? "NO (send SET)" : "YES");
}

bool parseDateTime(const char *text, RtcDateTime &value);

void handleKeypadKey(char key) {
  if (alarmRinging) {
    if (key == 'A' || key == '#') {
      stopAlarmRing();
      if (key == '#') {
        screen = Screen::MainMenu;
      }
      drawLcd();
    }
    return;
  }
  if (key == '#') {
    screen = Screen::MainMenu;
    drawLcd();
    return;
  }
  if (key == '*' && (screen == Screen::Clock || screen == Screen::ClockFormat)) {
    if (!rtcReady) {
      showLcd("RTC not found", "#:Menu");
      return;
    }
    memcpy(clockEntry, CLOCK_ENTRY_TEMPLATE, sizeof(clockEntry));
    clockDigitsEntered = 0;
    screen = Screen::SetClock;
    drawLcd();
    return;
  }

  switch (screen) {
    case Screen::MainMenu:
      if (key == 'A') {
        screen = Screen::ClockFormat;
      } else if (key == 'B') {
        screen = Screen::Stopwatch;
      } else if (key == 'C') {
        screen = Screen::AlarmMenu;
      } else {
        return;
      }
      break;
    case Screen::ClockFormat:
      if (key == '1' || key == '0') {
        militaryTimeDisplay = (key == '0');
        screen = Screen::Clock;
      } else {
        return;
      }
      break;
    case Screen::Clock:
      return;
    case Screen::Stopwatch:
      if (key == 'A') {
        if (stopwatch.isRunning()) {
          stopwatch.pause();
        } else {
          stopwatch.start();
        }
      } else if (key == 'B') {
        stopwatch.reset();
      } else {
        return;
      }
      break;
    case Screen::SetClock:
      if (key >= '0' && key <= '9' && clockDigitsEntered < CLOCK_DIGIT_COUNT) {
        clockEntry[CLOCK_DIGIT_POSITIONS[clockDigitsEntered]] = key;
        ++clockDigitsEntered;
      } else if (key == 'B' && clockDigitsEntered > 0) {
        --clockDigitsEntered;
        clockEntry[CLOCK_DIGIT_POSITIONS[clockDigitsEntered]] = '_';
      } else if (key == 'A' && clockDigitsEntered == CLOCK_DIGIT_COUNT) {
        RtcDateTime value = {};
        if (!parseDateTime(clockEntry, value)) {
          showLcd("Invalid entry", "B:Edit   #:Menu");
          return;
        }
        if (!rtcSetDateTime(value)) {
          showLcd("RTC write error", "A:Retry  #:Menu");
          return;
        }
        rebaseAlarmAfterClockSet(value);
        screen = Screen::ClockFormat;
      } else {
        return;
      }
      break;
    case Screen::AlarmMenu:
      if (key == 'A') {
        memset(alarmDigits, '_', sizeof(alarmDigits));
        alarmDigitsEntered = 0;
        screen = Screen::SetAlarm;
      } else if (key == 'B') {
        clockAlarm.cancel();
      } else {
        return;
      }
      break;
    case Screen::SetAlarm:
      if (key >= '0' && key <= '9' && alarmDigitsEntered < sizeof(alarmDigits)) {
        alarmDigits[alarmDigitsEntered++] = key;
      } else if (key == 'B' && alarmDigitsEntered > 0) {
        alarmDigits[--alarmDigitsEntered] = '_';
      } else if (key == 'A' && alarmDigitsEntered == sizeof(alarmDigits)) {
        const uint8_t hour = static_cast<uint8_t>((alarmDigits[0] - '0') * 10 + (alarmDigits[1] - '0'));
        const uint8_t minute = static_cast<uint8_t>((alarmDigits[2] - '0') * 10 + (alarmDigits[3] - '0'));
        if (hour >= 24 || minute >= 60) {
          showLcd("Invalid HHMM", "B:Edit  #:Menu");
          return;
        }
        RtcDateTime current = {};
        if (!readTrustedRtcTime(current)) {
          showLcd("RTC needs setup", "#:Menu then A *");
          return;
        }
        if (!clockAlarm.arm(rtcSecondsSince2000(current), hour, minute)) {
          showLcd("Alarm date error", "B:Edit  #:Menu");
          return;
        }
        lastAlarmCheckMs = millis();
        screen = Screen::AlarmMenu;
      } else {
        return;
      }
      break;
  }
  drawLcd();
}

void pollKeypad() {
  const uint32_t now = millis();
  if (!keypadReady || now - lastKeyPollMs < KEYPAD_POLL_MS) {
    return;
  }
  lastKeyPollMs = now;

  const uint8_t sample = keypad.getKey();
  if (sample != keyCandidate) {
    keyCandidate = sample;
    keyCandidateSinceMs = now;
  }
  if (sample > I2C_KEYPAD_NOKEY || now - keyCandidateSinceMs < KEYPAD_STABLE_MS) {
    return;
  }
  if (sample == I2C_KEYPAD_NOKEY) {
    keyArmed = true;
  } else if (keyArmed) {
    keyArmed = false;
    handleKeypadKey(KEYPAD_MAP[sample]);
  }
}

void printStatus() {
  uint8_t control = 0;
  uint8_t status = 0;
  if (!rtcGetStatus(control, status)) {
    Serial.println("STATUS: I2C read failed.");
    return;
  }
  RtcDateTime value = {};
  const bool calendarValid = rtcGetDateTime(value);
  const uint32_t ticks = getRtcTicks();
  const bool recentTick = ticks != 0 && static_cast<uint32_t>(millis() - lastTickMs) < 3000;
  Serial.printf(
      "STATUS: I2C=OK control=0x%02X OSF=%u calendar=%s SQW_1Hz=%s ticks=%lu recent_tick=%s\n",
      static_cast<unsigned int>(control), static_cast<unsigned int>((status >> 7) & 1),
      calendarValid ? "VALID" : "INVALID_OR_READ_ERROR",
      (control & 0x1C) == 0 ? "YES" : "NO",
      static_cast<unsigned long>(ticks), recentTick ? "YES" : "NO");
  Serial.println("Backup-cell behavior and SQW voltage require external checks.");
}

bool parseDateTime(const char *text, RtcDateTime &value) {
  if (strlen(text) != 19 || text[4] != '-' || text[7] != '-' ||
      text[10] != ' ' || text[13] != ':' || text[16] != ':') {
    return false;
  }
  for (size_t i = 0; i < 19; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) {
      continue;
    }
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  value.year = static_cast<uint16_t>((text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                                     (text[2] - '0') * 10 + text[3] - '0');
  value.month = static_cast<uint8_t>((text[5] - '0') * 10 + text[6] - '0');
  value.day = static_cast<uint8_t>((text[8] - '0') * 10 + text[9] - '0');
  value.hour = static_cast<uint8_t>((text[11] - '0') * 10 + text[12] - '0');
  value.minute = static_cast<uint8_t>((text[14] - '0') * 10 + text[15] - '0');
  value.second = static_cast<uint8_t>((text[17] - '0') * 10 + text[18] - '0');
  return rtcDateTimeValid(value);
}

void handleCommand(const char *command) {
  if (strcmp(command, "HELP") == 0) {
    printHelp();
    return;
  }
  if (strncmp(command, "KEY ", 4) == 0) {
    if (strlen(command) == 5 && strchr(KEYPAD_MAP, command[4]) != nullptr) {
      handleKeypadKey(command[4]);
    } else {
      Serial.println("ERROR: Send one keypad character, e.g. KEY A.");
    }
    return;
  }
  if (handleStopwatchCommand(command)) {
    return;
  }
  if (!rtcReady) {
    Serial.println("RTC not ready. Disconnect USB before checking wiring, then reset.");
    return;
  }
  if (strcmp(command, "GET") == 0) {
    printCurrentTime();
  } else if (strncmp(command, "FORMAT ", 7) == 0) {
    const char *requestedFormat = command + 7;
    if (strcmp(requestedFormat, "MILITARY") == 0) {
      militaryTimeDisplay = true;
    } else if (strcmp(requestedFormat, "24H") == 0) {
      militaryTimeDisplay = false;
    } else {
      Serial.println("ERROR: Use FORMAT 24H or FORMAT MILITARY.");
      return;
    }
    printCurrentTime();
    if (screen == Screen::Clock) {
      drawLcd();
    }
  } else if (strcmp(command, "STATUS") == 0) {
    printStatus();
  } else if (strcmp(command, "STREAM ON") == 0) {
    clockStream = true;
    screen = Screen::Clock;
    Serial.println("Time stream ON.");
    drawLcd();
  } else if (strcmp(command, "STREAM OFF") == 0) {
    clockStream = false;
    Serial.println("Time stream OFF. Tick counting continues.");
  } else if (strncmp(command, "SET ", 4) == 0) {
    RtcDateTime value = {};
    if (!parseDateTime(command + 4, value)) {
      Serial.println("ERROR: Use SET YYYY-MM-DD HH:MM:SS; valid date, years 2000-2099.");
      return;
    }
    if (!rtcSetDateTime(value)) {
      Serial.println("ERROR: Time write or verification failed. Send STATUS and GET.");
      return;
    }
    rebaseAlarmAfterClockSet(value);
    Serial.println("SET OK. Calendar verified, OSF cleared. Tick counter was not reset.");
    printCurrentTime();
    if (screen == Screen::Clock) {
      drawLcd();
    }
  } else {
    Serial.println("Unknown command. Send HELP. Commands are uppercase.");
  }
}

void readCommands() {
  // Limit work per loop iteration; never wait for a complete serial line.
  for (uint8_t budget = 0; budget < 64 && Serial.available() > 0; ++budget) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r' || ch == '\n') {
      if (commandOverflow) {
        Serial.println("ERROR: Command too long. Send the complete command again.");
      } else if (commandLength > 0) {
        commandBuffer[commandLength] = '\0';
        handleCommand(commandBuffer);
      }
      commandLength = 0;
      commandOverflow = false;
    } else if (!commandOverflow) {
      if (commandLength < sizeof(commandBuffer) - 1) {
        commandBuffer[commandLength++] = ch;
      } else {
        commandOverflow = true;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.printf("RTC + keypad + LCD + alarm. SDA GPIO%u, SCL GPIO%u, SQW GPIO%u.\n",
                static_cast<unsigned int>(SDA_PIN), static_cast<unsigned int>(SCL_PIN),
                static_cast<unsigned int>(SQW_PIN));
  if (Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Wire.setTimeOut(50);
    Wire.beginTransmission(LCD_ADDRESS);
    lcdReady = Wire.endTransmission() == 0;
    if (lcdReady) {
      lcd.init();
      lcd.backlight();
    }
    rtcReady = rtcBegin(Wire, SQW_PIN);
    keypadReady = keypad.begin();
  }
  drawLcd();
  alarmAudioBegin();
  lastTickMs = millis();
  lastWarningMs = lastTickMs;
  if (rtcReady) {
    Serial.println("RTC READY: address 0x68; SQW set to 1 Hz.");
    printStatus();
    printCurrentTime();
  } else {
    Serial.println("RTC INIT FAILED. Disconnect USB and check 3V3/GND/SDA/SCL.");
  }
  Serial.println(keypadReady ? "KEYPAD READY: address 0x20."
                            : "Keypad not found at 0x20. Use KEY commands to test the menu.");
  Serial.println(lcdReady ? "LCD READY: address 0x27." : "LCD not found at 0x27.");
  printHelp();
}

void loop() {
  readCommands();
  pollKeypad();
  bool newRtcTick = false;
  if (rtcReady) {
    const uint32_t ticks = getRtcTicks();
    if (ticks != lastTicks) {
      newRtcTick = true;
      lastTicks = ticks;
      lastTickMs = millis();
      if (clockStream && screen == Screen::Clock) {
        printCurrentTime();
      }
    }
    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - lastTickMs) >= 5000 &&
        static_cast<uint32_t>(nowMs - lastWarningMs) >= 5000) {
      Serial.println("NO SQW TICKS FOR 5 s. Check the selected SQW pin and its pull-up. GET uses I2C.");
      lastWarningMs = nowMs;
    }
  }
  updateAlarm(newRtcTick);
  const uint32_t sinceLcdRefresh = millis() - lastLcdRefreshMs;
  if (!alarmRinging && screen == Screen::Clock && (newRtcTick || sinceLcdRefresh >= 1000)) {
    drawLcd();
  } else if (!alarmRinging && screen == Screen::Stopwatch && stopwatch.isRunning() && sinceLcdRefresh >= 100) {
    drawLcd();
  }
  delay(1);
}
