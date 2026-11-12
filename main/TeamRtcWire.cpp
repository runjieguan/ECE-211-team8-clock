#include <Wire.h>
#include "TeamRtc.h"

bool team_rtc_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, uint8_t count) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, count) != count) {
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        data[i] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}

bool team_rtc_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data, uint8_t count) {
    Wire.beginTransmission(address);
    const bool buffered = Wire.write(reg) == 1 && Wire.write(data, count) == count;
    return Wire.endTransmission() == 0 && buffered;
}
