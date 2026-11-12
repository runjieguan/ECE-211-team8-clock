//-----------------------------------------------------------------------------
//                                     +
//                                   +JMJ+
// 
// Author: Matthew Perry
// Date:   11/10/26
// Description: 
//      The following code is the first major iteration for the overal clock
//      code for Team 8's practicum project ECE211 F2026 (Portland State
//      University).  The basic idea is that the clock will have a universal
//      "mode" variable which will act as the state variable for our FSM.
// Version History:
//      - A0.1: Added the set_program fxn.  This fxn calls the set_format and
//              set_alarm fxns.  
//      - A0.2: Integrated the audio code.  Split the program up into multiple 
//                                          files
//-----------------------------------------------------------------------------
#include <Wire.h>
#include "I2CKeyPad.h"
#include <stdlib.h>
#include "keypad_input.h"
#include "alarm.h"
#include "Timer_With_Audio.h"
#include "TeamRtc.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module in Arduino IDE."
#endif

// 16 keys, then N (no key) and F (fail). Pick the one for your keypad.
char keymap[19] = "D#0*C987B654A321NF";     // heavy-duty keypad
const int sda_pin = 8;
const int scl_pin = 9;
const int clk_pin = 5; // DS3231 SQW.
static_assert(sda_pin != scl_pin && clk_pin != sda_pin && clk_pin != scl_pin,
              "SDA, SCL and SQW must use different GPIO pins.");

// ---------------
volatile int num_time = 0;
int mode = 0;
int last_time = 0;
int time_format = 0;
bool alarm_enabled = false;
//---------------
char alarm_time[5] = {0};
uint32_t alarm_at_minute = 0;

int audio_duration = 10;
bool play_audio = false;
int audio_start_time = 0;


//----------------
void ISR_clk(){
    num_time++;
};
// ---------------

I2CKeyPad keyPad(0x20);     // fixed on the TK85; the compact adapter ships here
unsigned long last_input_time = 0;
const unsigned long input_delay = 300; // 500 ms


void set_clock_fxn(char user_input, int* mode){
    if (user_input == 'A'){
        *mode = 2;
        Serial.println("You're in basic clock mode (2)!");
    }
    else if (user_input == 'B'){
        *mode = 3; // set mode to 3 to ask for time format 
        Serial.println("You're in advanced clock mode (3)!");
        Serial.println("What time format would you like?");
        Serial.println("1 = Standard, 0 = Millitary");
    }
}

void set_program_mode(char user_input, int* mode, int* time_format, bool*
                      alarm_enabled, char* alarm_time) {
    /*
    set_program_mode handles the modes that modfy the mode variable.  That is
    why set_program_mode doesn't have a case for mode 2 because that mode
    doesn't set any more additional modes.
    */
    switch(*mode){
        case 1:
            set_clock_fxn(user_input, mode);         
            break;
        case 3:
            set_format(user_input, mode, time_format);
            break;
        case 4:
            set_alarm(user_input, mode, alarm_enabled);
            break;
        case 5:
            set_alarm_time(mode, user_input, alarm_time);
            break;
    }
}



void set_format(char user_input, int* mode, int* time_format){
    if (user_input == '1'){
        Serial.println("Standard time selected!");
        *mode = 4; // now set mode to 4 to ask for alarm
        *time_format = 0;
        Serial.println("Would you like an alarm? (1-yes, 0,-no)");
    }
    if (user_input == '0'){
        Serial.println("Millitary time selected!");
        *mode = 4;
        *time_format = 1;
        Serial.println("Would you like an alarm? (1-yes, 0,-no)");
    }
}

static void arm_rtc_alarm() {
    TeamRtcTime now;
    if (team_rtc_read(&now) != TEAM_RTC_OK) {
        alarm_enabled = false;
        Serial.println("Alarm not set. Check or set the DS3231 time, then try again.");
        return;
    }

    constexpr uint32_t minutes_per_day = 24U * 60U;
    const uint32_t now_minute = team_rtc_calendar_minute(&now);
    const int hhmm = atoi(alarm_time);
    alarm_at_minute = now_minute - now_minute % minutes_per_day + (hhmm / 100) * 60 + hhmm % 100;
    if (alarm_at_minute <= now_minute) {
        alarm_at_minute += minutes_per_day;
    }
    if (alarm_at_minute >= 36525U * minutes_per_day) {
        alarm_enabled = false;
        Serial.println("Alarm not set. Target date exceeds the supported RTC calendar.");
    }
}


void setup() {
    Serial.begin(115200);
    Wire.begin(sda_pin, scl_pin, 100000);     // the PCF8574 is rated for 100 kHz
    delay(10); // serial behaves weirdly if you don't let it sit for a bit so
               //that's why there is a delay here
    if (team_rtc_begin()) {
        Serial.println("DS3231 ready at 0x68. SQW configured for 1 Hz.");
    } else {
        Serial.println("DS3231 setup failed. Check RTC power, SDA and SCL.");
    }
    while (!keyPad.begin()){
        // Sit here untill the keypad is pluged in
        Serial.println("No keypad at 0x20");
        delay(1000);
    }
    
    keyPad.loadKeyMap(keymap);

    pinMode(clk_pin, INPUT_PULLUP);
    attachInterrupt(
        digitalPinToInterrupt(clk_pin),
        ISR_clk,
        RISING
    );    
    setupAudio();
}


void loop() {
    char c = keyPad.getChar();
    if (millis() - last_input_time >= input_delay){
        if (c != 'N' && c != 'F'){ // make sure it's not N (no in) or F (fail) 
            Serial.println(c);
            // handle the input given what mode you're in and the input  
            if (c == '#') {
                mode = 1;
                Serial.println("You're in Program mode (1)!");
            }
            const bool entering_alarm_time = mode == 5;
            set_program_mode(c, &mode, &time_format, &alarm_enabled, alarm_time);
            if (entering_alarm_time && mode == 6 && alarm_enabled) {
                arm_rtc_alarm();
            }
            last_input_time = millis();           
        }
    }
    const int ticks = num_time;
    if (play_audio && ticks - audio_start_time >= audio_duration) {
        stopAudio();
        play_audio = false;
    }

    if ((mode != 2 && mode != 6) || ticks == last_time) {
        return;
    }
    last_time = ticks;

    TeamRtcTime now;
    const TeamRtcResult result = team_rtc_read(&now);
    if (result == TEAM_RTC_IO_ERROR) {
        Serial.println("RTC read failed. Check SDA and SCL.");
        return;
    }
    if (result == TEAM_RTC_TIME_INVALID) {
        Serial.println("RTC needs setup. Set the current time using RTC_Only_C.");
        return;
    }

    if (mode == 6 && alarm_enabled && !play_audio) {
        const uint32_t now_minute = team_rtc_calendar_minute(&now);
        if (now_minute == alarm_at_minute) {
            playAudio();
            audio_start_time = ticks;
            play_audio = true;
        }
        if (now_minute >= alarm_at_minute) {
            alarm_enabled = false; // One-shot alarm, even if mode 6 is left and later resumed.
        }
    }

    if (mode == 6 && time_format == 1) {
        Serial.printf("%02u%02u\n", (unsigned)now.hour, (unsigned)now.minute);
    } else {
        Serial.printf("%02u:%02u:%02u\n", (unsigned)now.hour, (unsigned)now.minute, (unsigned)now.second);
    }
}




