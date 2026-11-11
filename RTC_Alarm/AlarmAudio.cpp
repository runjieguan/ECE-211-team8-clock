#include <Arduino.h>
#include "AlarmAudio.h"

namespace {

// GPIO numbers on both boards. Connect the module TX to RX and its RX to TX.
constexpr int8_t MP3_RX_GPIO = 18;
constexpr int8_t MP3_TX_GPIO = 17;
constexpr uint32_t STARTUP_DELAY_MS = 1000;
constexpr uint32_t SD_SELECT_DELAY_MS = 500;
constexpr uint8_t STARTUP_VOLUME = 20;
constexpr uint16_t ALARM_TRACK_INDEX = 1;

enum class StartupState { Idle, SelectSd, SetVolume, Ready };
StartupState startupState = StartupState::Idle;
uint32_t startupStepMs = 0;

// UART frame and commands adapted from the team's Timer_With_Audio.cpp.
bool sendCommand(uint8_t command, uint16_t parameter) {
  const uint8_t highByte = static_cast<uint8_t>(parameter >> 8);
  const uint8_t lowByte = static_cast<uint8_t>(parameter);
  constexpr uint8_t feedback = 0x01;
  const uint16_t checksum = static_cast<uint16_t>(
      -(0xFF + 0x06 + command + feedback + highByte + lowByte));
  const uint8_t packet[] = {
      0x7E, 0xFF, 0x06, command, feedback, highByte, lowByte,
      static_cast<uint8_t>(checksum >> 8), static_cast<uint8_t>(checksum), 0xEF};
  return Serial2.write(packet, sizeof(packet)) == sizeof(packet);
}

}  // namespace

void alarmAudioBegin() {
  Serial2.begin(9600, SERIAL_8N1, MP3_RX_GPIO, MP3_TX_GPIO);
  startupStepMs = millis();
  startupState = StartupState::SelectSd;
}

void alarmAudioUpdate(uint32_t nowMs) {
  const uint32_t elapsedMs = nowMs - startupStepMs;
  if (startupState == StartupState::SelectSd && elapsedMs >= STARTUP_DELAY_MS &&
      sendCommand(0x09, 0x0002)) {
    startupStepMs = nowMs;
    startupState = StartupState::SetVolume;
  } else if (startupState == StartupState::SetVolume && elapsedMs >= SD_SELECT_DELAY_MS &&
             sendCommand(0x06, STARTUP_VOLUME)) {
    startupState = StartupState::Ready;
  }
}

bool alarmAudioPlay() {
  return startupState == StartupState::Ready && sendCommand(0x03, ALARM_TRACK_INDEX);
}

void alarmAudioStop() {
  if (startupState == StartupState::Ready) {
    sendCommand(0x16, 0);
  }
}
