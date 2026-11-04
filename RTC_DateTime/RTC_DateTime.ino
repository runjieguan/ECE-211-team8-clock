#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include "RtcClock.h"

constexpr uint8_t SDA_PIN = 4;
constexpr uint8_t SCL_PIN = 5;
constexpr uint8_t SQW_PIN = 6;

bool rtcReady = false;
bool clockStream = true;
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
  Serial.println("SET changes SQW phase. Pause timing features before setting time.");
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
  Serial.printf(
      "ticks=%lu time=%04u-%02u-%02u %02u:%02u:%02u trusted=%s\n",
      static_cast<unsigned long>(getRtcTicks()),
      static_cast<unsigned int>(value.year), static_cast<unsigned int>(value.month),
      static_cast<unsigned int>(value.day), static_cast<unsigned int>(value.hour),
      static_cast<unsigned int>(value.minute), static_cast<unsigned int>(value.second),
      (status & 0x80) ? "NO (send SET)" : "YES");
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
  Serial.println("Battery backup and SQW voltage have not been verified.");
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
  if (!rtcReady) {
    Serial.println("RTC not ready. Disconnect USB before checking wiring, then reset.");
    return;
  }
  if (strcmp(command, "GET") == 0) {
    printCurrentTime();
  } else if (strcmp(command, "STATUS") == 0) {
    printStatus();
  } else if (strcmp(command, "STREAM ON") == 0) {
    clockStream = true;
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
  Serial.println("RTC stage 2: calendar time + 1 Hz ticks. ESP32-S3 pins 4/5/6.");
  if (Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Wire.setTimeOut(50);
    rtcReady = rtcBegin(Wire, SQW_PIN);
  }
  lastTickMs = millis();
  lastWarningMs = lastTickMs;
  if (rtcReady) {
    Serial.println("RTC READY: address 0x68; SQW set to 1 Hz on GPIO6.");
    printStatus();
    printCurrentTime();
  } else {
    Serial.println("RTC INIT FAILED. Disconnect USB and check 3V3/GND/SDA4/SCL5.");
  }
  printHelp();
}

void loop() {
  if (rtcReady) {
    const uint32_t ticks = getRtcTicks();
    if (ticks != lastTicks) {
      lastTicks = ticks;
      lastTickMs = millis();
      if (clockStream) {
        printCurrentTime();
      }
    }
    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - lastTickMs) >= 5000 &&
        static_cast<uint32_t>(nowMs - lastWarningMs) >= 5000) {
      Serial.println("NO SQW TICKS FOR 5 s. GET still works if I2C is connected; check SQW6/pull-up.");
      lastWarningMs = nowMs;
    }
  }
  readCommands();
  delay(1);
}
