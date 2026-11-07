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
#include <ctype.h> // needed for isdigit fxn for input validation
#include <stdlib.h>

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

// string input variables
char char_string_input[100] = {0};
int string_iteration_count = 0;
char alarm_time[5] = {0}; // 5 because need to account for \0

//----------------
void ISR_clk(){
    num_time++;
};
// ---------------

I2CKeyPad keyPad(0x20);     // fixed on the TK85; the compact adapter ships here
unsigned long last_input_time = 0;
const unsigned long input_delay = 500; // 500 ms

int get_string_input(char user_input, int length_of_input){
    char_string_input[string_iteration_count] = user_input;
    string_iteration_count++;

    if (string_iteration_count == length_of_input){
        char_string_input[string_iteration_count] = '\0';
        // the above code may or may not help.... so we'll leave it out 
        string_iteration_count = 0;
        return 0;
    }

    return 1;
 }

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

void set_alarm_time(int* mode, char user_input){
    int length_of_input_string = 4;
    int user_input_finished = get_string_input(user_input, 
                                               length_of_input_string);
    if (user_input_finished == 0){
        // Serial.println(char_string_input);
        if (verify_alarm_time_input(length_of_input_string) == 1){
            Serial.println("Invalid string, please try again");
            *mode = 15;
        } 
        else {
            *mode = 6;
            // the less than or equal will include the \0 character
            for (int i = 0; i <= length_of_input_string; i++){
                alarm_time[i] = char_string_input[i];
            }
            Serial.println(alarm_time);    
        }
    }
}


int verify_alarm_time_input(int length_of_input_string){
    int string_digit = 1;
    for (int i = 0; i < length_of_input_string; i++){
        string_digit = isdigit(char_string_input[i]);
        if (string_digit == 0){
            return 1;
        }
    }
    // the - '0' is needed to convert it into a number for comparison
    if (char_string_input[0] - '0' > 1){
        return 1;
    }
    if (char_string_input[1] - '0' > 2){
        return 1;
    }
    if (char_string_input[2] - '0' > 5){
        return 1;
    }
    return 0;
}


void set_program_mode(char user_input, int* mode, int* time_format, bool*
                      alarm_enabled) {
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
        case 15:
            set_alarm_time(mode, user_input);
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


void set_alarm(char user_input, int* mode, bool* alarm_enabled){
    if (user_input == '1'){
        Serial.println("Alarm mode activated");
        Serial.print("Please Enter your alarm time: ");
        *alarm_enabled = true; 
        *mode = 15;
    }
    if (user_input == '0'){
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
    if (millis() - last_input_time >= input_delay){
        if (c != 'N' && c != 'F'){ // make sure it's not N (no in) or F (fail) 
            Serial.println(c);
            // handle the input given what mode you're in and the input  
            if (c == '#') {
                mode = 1;
                Serial.println("You're in Program mode (1)!");
            }
            set_program_mode(c, &mode, &time_format, &alarm_enabled);
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
        if (num_time == atoi(alarm_time)){
            Serial.println("ALARM!!!!!!");
        }
        if (num_time != last_time){
            Serial.println(num_time);
            last_time = num_time;
        }
    }
}




