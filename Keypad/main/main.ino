//-----------------------------------------------------------------------------
//                                     +
//                                   +JMJ+
// 
// Author: Matthew Perry
// Date:   11/5/26
// Description: 
//      The following code is the first major iteration for the overal clock
//      code for Team 8's practicum project ECE211 F2026 (Portland State
//      University).  The basic idea is that the clock will have a universal
//      "mode" variable which will act as the state variable for our FSM.
// Version History:
//      - A0.1: Added the set_program fxn.  This fxn calls the set_format and
//              set_alarm fxns.  
//-----------------------------------------------------------------------------
#include <Wire.h>
#include "I2CKeyPad.h"

// 16 keys, then N (no key) and F (fail). Pick the one for your keypad.
// char keymap[19] = "DCBA#9630852*741NF";            // soft keypad
char keymap[19] = "D#0*C987B654A321NF";     // heavy-duty keypad
const int clk_pin = 14;

// ---------------
volatile int num_time = 0;
int mode = 0;
int last_time = 0;
int time_format = 0;
bool alarm_enabled = false;
//---------------
//----------------
void ISR_clk(){
    num_time++;
};
// ---------------

I2CKeyPad keyPad(0x20);     // fixed on the TK85; the compact adapter ships here
char char_last = 'N';

void set_program_mode(char user_input, int* mode, int* time_format, bool*
                      alarm_enabled) {
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
    if (*mode == 3){
        set_format(user_input, mode, time_format);
    }
    if (*mode == 4){
        set_alarm(user_input, mode, alarm_enabled);
    }
    if (*mode == 5){
        // set_alarm_time(user_input, mode, );
        *mode = 6;
    }
}


void set_format(char user_input, int* mode, int* time_format){
    if (user_input == '1'){
        Serial.println("Standard time selected!");
        *mode = 4; // now set mode to 4 to ask for alarm
        *time_format = 0;
        Serial.println("Would you like an alarm? (2-yes, 3,-no)");
    }
    if (user_input == '0'){
        Serial.println("Millitary time selected!");
        *mode = 4;
        *time_format = 1;
        Serial.println("Would you like an alarm? (2-yes, 3,-no)");
    }
}


void set_alarm(char user_input, int* mode, bool* alarm_enabled){
    if (user_input == '2'){
        Serial.println("Alarm mode activated");
        *alarm_enabled = true; 
        *mode = 5;
    }
    if (user_input == '3'){
        Serial.println("No alarm");
        *alarm_enabled = false;
        *mode = 6;
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
}


void loop() {
    char c = keyPad.getChar();
    if (c != char_last){ // check for new input
        if (c != 'N' && c != 'F'){ // make sure it's not N (no in) or F (fail) 
            Serial.println(c);
            // handle the input given what mode you're in and the input  
            if (c == '#') {
                mode = 1;
                Serial.println("You're in Program mode (1)!");
            }
            if (mode == 1 || mode == 3 || mode == 4) {
                set_program_mode(c, &mode, &time_format, &alarm_enabled);
            } 
            char_last = c;
        }
    }
    // do other program stuff 
    if (mode == 2 || mode == 6 || mode == 5){
        if (num_time != last_time){
            Serial.println(num_time);
            last_time = num_time;
        }
    }
}



