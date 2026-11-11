#pragma once

#include <stdint.h>

void alarmAudioBegin();
void alarmAudioUpdate(uint32_t nowMs);
bool alarmAudioPlay();
void alarmAudioStop();
