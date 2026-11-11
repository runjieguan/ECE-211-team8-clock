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

// 16 keys, then N (no key) and F (fail). Pick the one for your keypad.
char keymap[19] = "D#0*C987B654A321NF";     // heavy-duty keypad
const int clk_pin = 5;

// ---------------
volatile int num_time = 0;
int mode = 0;
int last_time = 0;
int time_format = 0;
bool alarm_enabled = false;
//---------------
char alarm_time[5] = {0};

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
            set_program_mode(c, &mode, &time_format, &alarm_enabled, alarm_time);
            last_input_time = millis();           
        }
    }
    // do other program stuff 
    if (mode == 2){
        if (num_time != last_time){
            Serial.println(num_time);
            last_time = num_time;
        }
    }
    if (mode == 6){
        if (num_time == atoi(alarm_time) && alarm_enabled == true 
                                         && play_audio == false){
            
            playAudio();
            audio_start_time = num_time;
            play_audio = true;
        }
        if (play_audio == true){
            if(num_time - audio_start_time == audio_duration){
                stopAudio();
                play_audio = false;
            }
        }
        if (num_time != last_time){
            Serial.println(num_time);
            last_time = num_time;
        }
    }
}




