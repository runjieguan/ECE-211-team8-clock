#include <Arduino.h>
#include <Wire.h>
#include <I2CKeyPad.h>
#include <stdio.h>
#include <string.h>
#include "RtcClock.h"
#include "Stopwatch.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr uint8_t SDA_PIN = 4;
constexpr uint8_t SCL_PIN = 5;
constexpr uint8_t SQW_PIN = 6;
#elif defined(CONFIG_IDF_TARGET_ESP32)
// Select the Adafruit ESP32 Feather board. GPIO14 follows the team's main.ino.
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

enum class Screen { MainMenu, ClockFormat, Clock, Stopwatch };
Screen screen = Screen::MainMenu;
I2CKeyPad keypad(KEYPAD_ADDRESS);
bool keypadReady = false;
uint32_t lastKeyPollMs = 0;
uint32_t keyCandidateSinceMs = 0;
uint8_t keyCandidate = I2C_KEYPAD_NOKEY;
bool keyArmed = true;

Stopwatch stopwatch;
uint32_t lastStopwatchPrintMs = 0;

bool rtcReady = false;
bool clockStream = true;
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
  Serial.println("FORMAT 24H (08:00:00) | FORMAT MILITARY (800)");
  Serial.println("SW START | SW PAUSE | SW RESET | SW GET");
  Serial.println("KEY <key> simulates one keypad press, e.g. KEY #");
  Serial.println("Stopwatch: MM:SS.cc; automatic refresh about 100 ms.");
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
  lastStopwatchPrintMs = millis();
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
    snprintf(timeText, sizeof(timeText), "%u%02u", hour, minute);
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

void printMainMenu() {
  Serial.println("Main menu: A = Clock, B = Stopwatch");
}

void handleKeypadKey(char key) {
  if (key == '#') {
    screen = Screen::MainMenu;
    printMainMenu();
    return;
  }

  switch (screen) {
    case Screen::MainMenu:
      if (key == 'A') {
        screen = Screen::ClockFormat;
        Serial.println("Clock format: 1 = 24-hour (08:00:00), 0 = Military (800), # = Menu");
      } else if (key == 'B') {
        screen = Screen::Stopwatch;
        Serial.println("Stopwatch: A = Start/Pause/Resume, B = Reset, # = Menu");
        printStopwatch();
      }
      break;
    case Screen::ClockFormat:
      if (key == '1' || key == '0') {
        militaryTimeDisplay = (key == '0');
        screen = Screen::Clock;
        clockStream = true;
        Serial.println("Clock: # = Menu");
        printCurrentTime();
      }
      break;
    case Screen::Clock:
      break;
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
        break;
      }
      printStopwatch();
      break;
  }
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
  } else if (strcmp(command, "STATUS") == 0) {
    printStatus();
  } else if (strcmp(command, "STREAM ON") == 0) {
    clockStream = true;
    screen = Screen::Clock;
    Serial.println("Time stream ON.");
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
    Serial.println("SET OK. Calendar verified, OSF cleared. Tick counter was not reset.");
    printCurrentTime();
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
  delay(1500);
  Serial.printf("RTC + keypad experiment. SDA GPIO%u, SCL GPIO%u, SQW GPIO%u.\n",
                static_cast<unsigned int>(SDA_PIN), static_cast<unsigned int>(SCL_PIN),
                static_cast<unsigned int>(SQW_PIN));
  if (Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Wire.setTimeOut(50);
    rtcReady = rtcBegin(Wire, SQW_PIN);
    keypadReady = keypad.begin();
  }
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
  printHelp();
  printMainMenu();
}

void loop() {
  readCommands();
  pollKeypad();
  if (rtcReady) {
    const uint32_t ticks = getRtcTicks();
    if (ticks != lastTicks) {
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
  const uint32_t nowStopwatchMs = millis();
  if (screen == Screen::Stopwatch && stopwatch.isRunning() &&
      static_cast<uint32_t>(nowStopwatchMs - lastStopwatchPrintMs) >= 100) {
    printStopwatch();
  }
  delay(1);
}
