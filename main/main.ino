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
#include <RTClib.h>
#include "esp_timer.h"

// 16 keys, then N (no key) and F (fail). Pick the one for your keypad.
char keymap[19] = "D#0*C987B654A321NF";     // heavy-duty keypad
const int clk_pin = 5;

RTC_DS3231 rtc; // initalize the rtc we're using 

// ---------------
volatile int num_tick = 0;
volatile int num_time = 0;

int mode = KEYPAD_MENU;
int last_time = 0;
int last_tick = 0;
int time_format = 0;
bool alarm_enabled = false;
//---------------
char alarm_time[5] = {0};

int audio_duration = 10;
bool play_audio = false;
int audio_start_time = 0;


//----------------
void ISR_clk(){
    num_tick++;
};
// ---------------

I2CKeyPad keyPad(0x20);     // fixed on the TK85; the compact adapter ships here

void print_menu() {
    Serial.println("Menu: 1 or A = Clock, 2 = Stopwatch, B = Advanced Clock / Alarm");
    Serial.println("# = Menu from any mode");
}

void print_stopwatch(bool force) {
    static unsigned long last_print_ms = 0;
    static uint64_t last_value = 0;
    const unsigned long now_ms = millis();
    if (!force && now_ms - last_print_ms < 100) {
        return;
    }
    last_print_ms = now_ms;

    const uint64_t cs = keypad_stopwatch_centiseconds((uint64_t)esp_timer_get_time());
    if (force || cs != last_value) {
        Serial.printf("Stopwatch %02llu:%02u.%02u\n", (unsigned long long)(cs / 6000U),
                      (unsigned)((cs / 100U) % 60U), (unsigned)(cs % 100U));
        last_value = cs;
    }
}


void set_clock_fxn(char user_input, int* mode){
    if (user_input == 'A'){
        *mode = KEYPAD_CLOCK;
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
        case KEYPAD_MENU:
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


void setup() {
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(100000);     // the PCF8574 is rated for 100 kHz
    delay(10); // serial behaves weirdly if you don't let it sit for a bit so
               //that's why there is a delay here
    while (!keyPad.begin()){
        // Sit here untill the keypad is pluged in
        Serial.println("No keypad at 0x20");
        delay(1000);
    }
    
    keyPad.loadKeyMap(keymap);
    
    attachInterrupt(
        digitalPinToInterrupt(clk_pin),
        ISR_clk,
        RISING
    );
    
    setupAudio();
    rtc.begin();
    print_menu();
}


void loop() {
    char c = keypad_take_press(keyPad.getChar(), millis());
    if (c != '\0') {
        Serial.println(c);
        const int next_mode = keypad_menu_mode(c, mode);
        if (c == '#') {
            if (mode == 5) {
                reset_string_input();
            }
            mode = next_mode;
            print_menu();
        } else if (next_mode != mode) {
            mode = next_mode;
            if (mode == KEYPAD_STOPWATCH) {
                Serial.println("Stopwatch: A = Start/Pause/Resume, B = Reset, # = Menu");
                print_stopwatch(true);
            } else {
                last_time = -1;
                Serial.println("You're in basic clock mode (2)!");
            }
        } else if (mode == KEYPAD_STOPWATCH) {
            if (keypad_stopwatch_key(c, (uint64_t)esp_timer_get_time())) {
                print_stopwatch(true);
            }
        } else {
            set_program_mode(c, &mode, &time_format, &alarm_enabled, alarm_time);
        }
    }

    if (mode == KEYPAD_STOPWATCH) {
        print_stopwatch(false);
        return;
    }

    DateTime now = rtc.now();
    num_time = now.second();

    // do other program stuff 
    if (mode == KEYPAD_CLOCK){
        if (num_time != last_time){
            Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n",
                  now.year(),
                  now.month(),
                  now.day(),
                  now.hour(),
                  now.minute(),
                  now.second());
            last_time = num_time;
        }
    }
    if (mode == 6){
        if (num_tick == atoi(alarm_time) && alarm_enabled == true 
                                         && play_audio == false){
            
            playAudio();
            audio_start_time = num_time;
            play_audio = true;
        }
        if (play_audio == true){
            if(num_tick - audio_start_time == audio_duration){
                stopAudio();
                play_audio = false;
            }
        }
        if (num_tick != last_tick){
            Serial.println(num_tick);
            last_tick = num_tick;
        }
    }
}




