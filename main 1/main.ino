//-----------------------------------------------------------------------------
//                                     +
//                                   +JMJ+
// 
// Author: Matthew Perry
// Date:   11/13/26
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
#include <lcd_screen.h>
#include "set_time.h"
#include "esp_timer.h"

// 16 keys, then N (no key) and F (fail). Pick the one for your keypad.
char keymap[19] = "D#0*C987B654A321NF";     // heavy-duty keypad
const int clk_pin = 5;
const int STOPWATCH_MODE = 9;


// ---------------
volatile int num_tick = 0;
volatile int num_time = 0;

int mode = 0;
int last_time = 0;
int last_tick = 0;
int time_format = 0;
bool alarm_enabled = false;
//---------------
char alarm_time[5] = {0};

int audio_duration = 10;
bool playing_audio = false;
unsigned long audio_start_time = 0;

unsigned long welcome_start_time = 0;
int welcome_scroll_screen_duration = 3000;
int welcome_screen_num = 0;

//----------------
void ISR_clk(){
    num_tick++;
};
// ---------------

I2CKeyPad keyPad(0x20);     // fixed on the TK85; the compact adapter ships here
unsigned long last_input_time = 0;
const unsigned long input_delay = 300; // 500 ms

static bool stopwatch_running = false;
static bool stopwatch_key_released = true;
static uint64_t stopwatch_started_us = 0;
static uint64_t stopwatch_elapsed_us = 0;

void show_stopwatch(uint64_t now_us, bool force) {
    static uint64_t last_refresh_us = 0;
    if (!force && (!stopwatch_running || now_us - last_refresh_us < 100000U)) {
        return;
    }
    last_refresh_us = now_us;

    uint64_t elapsed_us = stopwatch_elapsed_us;
    if (stopwatch_running) {
        elapsed_us += now_us - stopwatch_started_us;
    }
    const uint64_t cs = elapsed_us / 10000U;
    char text[17];
    snprintf(text, sizeof(text), "%02u:%02u.%02u", (unsigned)(cs / 6000U),
             (unsigned)((cs / 100U) % 60U), (unsigned)(cs % 100U));

    if (force) {
        if (stopwatch_running) {
            displayText("A:Pause B:Reset", 0);
        } else {
            displayText(cs == 0 ? "A:Start B:Reset" : "A:Resume B:Reset", 0);
        }
    }
    displayText(text, 1);
}

void set_stopwatch(char user_input) {
    if (!stopwatch_key_released || (user_input != 'A' && user_input != 'B')) {
        return;
    }
    stopwatch_key_released = false;
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    if (user_input == 'B') {
        stopwatch_running = false;
        stopwatch_elapsed_us = 0;
    } else if (stopwatch_running) {
        stopwatch_elapsed_us += now_us - stopwatch_started_us;
        stopwatch_running = false;
    } else {
        stopwatch_started_us = now_us;
        stopwatch_running = true;
    }
    show_stopwatch(now_us, true);
}

void set_program_mode(char user_input, int* mode, int* time_format, bool*
                      alarm_enabled, DateTime* now, char* alarm_time) {
    /*
    set_program_mode handles the modes that modfy the mode variable.  That is
    why set_program_mode doesn't have a case for mode 2 because that mode
    doesn't set any more additional modes.
    */
    switch(*mode){
        case 1:
            // show menu options
            show_program_option(user_input);
            set_clock_fxn(user_input, mode);         
            break;
        case 2:
            set_format(user_input, mode, time_format, true);
            break;
        case 3:
            set_format(user_input, mode, time_format, false);
            break;
        case 4:
            set_24hr_clock_time(mode, user_input, time_format, now);
            break;
        case 5:
            set_12hr_clock_time(mode, user_input, time_format, now);
            break;
        case 6:
            // FIXME: Check what time format you want and then get the user
            // alarm time accordingly
            set_alarm(user_input, mode, alarm_enabled);
            break;
        case 7:
            set_alarm_time(mode, user_input, alarm_time, time_format);
            break;
        case STOPWATCH_MODE:
            set_stopwatch(user_input);
            break;
    }
}


void show_program_option(char user_input) {

    static int MAX_OPTIONS = 2;
    static int menu_location = 0;
    static int old_menu_location = -1;

    if (user_input == '2') { // scroll up!
        menu_location--;
        if (menu_location < 0){
            menu_location = MAX_OPTIONS;
        }
    }
    else if (user_input == '3'){ // scroll down!
        menu_location++;
        if (menu_location > MAX_OPTIONS){
            menu_location = 0;
        }
    }
    // now look at what needs to be printed


    if (old_menu_location != menu_location){
        if (menu_location == 0){
            displayText("Select Mode", 0);
            displayText("2-UP 3-DWN C:SW", 1);
        }
        else if (menu_location == 1){
            displayText("Program clock", 0);
            displayText("Press B ", 1);
        }
        else if (menu_location == 2){
            displayText("Show clock", 0);
            displayText("Press A ", 1);
        }
        else {
            displayText("wot?", 0);
        }
        old_menu_location = menu_location;
    }
}

void set_clock_fxn(char user_input, int* mode){
    if (user_input == 'A'){
        *mode = 2;
        Serial.println("You're in basic clock mode (2)!");
        displayClear();
        displayText("Time format?", 0);
        displayText("1=12hr, 0=24hr", 1);
    }
    else if (user_input == 'B'){
        *mode = 3; // set mode to 3 to ask for time format 
        Serial.println("You're in advanced clock mode (3)!");
        Serial.println("What time format would you like?");
        Serial.println("1 = Standard, 0 = Millitary");
        displayText("Time format?", 0);
        displayText("1=12hr, 0=24hr", 1);
    }
    else if (user_input == 'C'){
        *mode = STOPWATCH_MODE;
        stopwatch_key_released = true;
        show_stopwatch((uint64_t)esp_timer_get_time(), true);
    }
}


void setup() {
    // Serial.begin(115200);
    Wire.begin();
    Wire.setClock(100000);     // the PCF8574 is rated for 100 kHz
    delay(10); // serial behaves weirdly if you don't let it sit for a bit so
               //that's why there is a delay here
    rtc.begin();
    setupLCD();

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

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
}



void loop() {
    char c = keyPad.getChar();
    if (mode == STOPWATCH_MODE && c == 'N') {
        stopwatch_key_released = true;
    }
    DateTime now = rtc.now();
    num_time = now.second();
    

    if (millis() - last_input_time >= input_delay){
        if (c != 'N' && c != 'F'){ // make sure it's not N (no in) or F (fail)             
            Serial.println(c);
            // handle the input given what mode you're in and the input  
            if (c == '#') {
                mode = 1;
                Serial.println("You're in Program mode (1)!");
                displayText("Select Mode", 0);
                displayText("2-UP 3-DWN C:SW", 1);
            }
            // helpful for testing things as you can skip a minute
            else if (c == '*'){
                rtc.adjust(DateTime(
                    now.year(),
                    now.month(),
                    now.day(),
                    now.hour(),
                    now.minute(),
                    59
                ));
            }
            set_program_mode(c, &mode, &time_format, &alarm_enabled, 
                                &now, alarm_time);
            last_input_time = millis();           
        }
    }
    if (mode == STOPWATCH_MODE) {
        show_stopwatch((uint64_t)esp_timer_get_time(), false);
    }
    if (mode == 0){
        if (millis() - welcome_start_time >= welcome_scroll_screen_duration){
            switch(welcome_screen_num){
                case 0:
                    displayText("Welcome to", 0);
                    displayText("Team 8's Clock!", 1);
                    welcome_screen_num++;
                    welcome_start_time = millis();
                    break;
                case 1:
                    displayText("Press # to", 0);
                    displayText("select mode", 1);
                    welcome_screen_num = 0;
                    welcome_start_time = millis();     
                    break;
            }

        }

    }
    // do other program stuff 
    if (mode == 8){
        if (time_format == 0){ // 12hr time 
            int hour_12 = ((now.hour() + 11) % 12) + 1;
            // A == am and B == pm 
            char period = (now.hour() < 12) ? 'A': 'B';
            if (num_time != last_time){
                switch(period){
                    case 'A': // AM time 
                        Serial.printf("%02d:%02d:%02d",
                              hour_12,
                              now.minute(),
                              now.second());
                        Serial.println(" AM");                       
                        displayTime(hour_12, now.minute(), now.second(),
                                    time_format, period);
                        break;
                    case 'B': // PM time 
                        Serial.printf("%02d:%02d:%02d",
                              hour_12,
                              now.minute(),
                              now.second());
                        Serial.println(" PM");
                        displayTime(hour_12, now.minute(), now.second(),
                                    time_format, period);
                        break;         
                }
                last_time = num_time;
            }        
        }
        if (time_format == 1){
            if (num_time != last_time){
                Serial.printf("%02d:%02d:%02d\n",
                      now.hour(),
                      now.minute(),
                      now.second());
                last_time = num_time;
                displayTime(now.hour(), now.minute(), now.second(),
                            time_format, 'N'); // N because period is
                                               // meangingless here

            }
        }
        if (alarm_enabled == true) {

            if (now.hour() == (alarm_time[0] - '0') * 10
                            + (alarm_time[1] - '0') &&
                now.minute() == (alarm_time[2] - '0') * 10
                              + (alarm_time[3] - '0') &&
                now.second() == 0 &&
                playing_audio == false) {

                Serial.println("ALARM TRIGGERED");

                playAudio();

                audio_start_time = millis();
                playing_audio = true;
            }

            if (playing_audio &&
                millis() - audio_start_time >= audio_duration * 1000) {

                Serial.println("STOPPING AUDIO");

                stopAudio();

                playing_audio = false;
                audio_start_time = 0;
            }
        }
    }
}





