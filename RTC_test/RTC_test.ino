#include <Wire.h>
#include <RTClib.h>
#include <Arduino.h>

RTC_DS3231 rtc;

void setup() {
    Serial.begin(9600);
    Wire.begin();
    delay(1000);
    Serial.println("Setup?");
    
    Serial.println("Scanning I2C...");

    for (byte address = 1; address < 127; address++) {
        Wire.beginTransmission(address);

        if (Wire.endTransmission() == 0) {
            Serial.print("Device found at 0x");
            Serial.println(address, HEX);
        }
    }
    // Initialize RTC
    rtc.begin();

    // Set RTC to compilation date/time
    // Uncomment this line ONCE to set the clock
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    // Configure SQW output to 1 Hz
    rtc.writeSqwPinMode(DS3231_SquareWave1Hz);
}

void loop() {
    // Read current date/time
    DateTime now = rtc.now();
    Serial.println("Loop?");
    Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n",
        now.year(),
        now.month(),
        now.day(),
        now.hour(),
        now.minute(),
        now.second()
    );

    delay(1000);
}
