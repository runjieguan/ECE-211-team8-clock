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

// 16 keys, then N (no key) and F (fail). Pick the one for your keypad.
char keymap[19] = "D#0*C987B654A321NF";     // heavy-duty keypad
const int clk_pin = 5;

RTC_DS3231 rtc; // initalize the rtc we're using 

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
bool play_audio = false;
int audio_start_time = 0;


//----------------
void ISR_clk(){
    num_tick++;
};
// ---------------

I2CKeyPad keyPad(0x20);     // fixed on the TK85; the compact adapter ships here
unsigned long last_input_time = 0;
const unsigned long input_delay = 300; // 500 ms


void set_clock_fxn(char user_input, int* mode){
    if (user_input == 'A'){
        *mode = 2;
        Serial.println("You're in basic clock mode (2)!");
        displayClear();
    }
    else if (user_input == 'B'){
        *mode = 3; // set mode to 3 to ask for time format 
        Serial.println("You're in advanced clock mode (3)!");
        Serial.println("What time format would you like?");
        Serial.println("1 = Standard, 0 = Millitary");
        displayText("Time format?", 0);
        displayText("1=12hr, 0=24hr", 1);
    }
}



int verify_clock_time_input_12hr(int length_of_input_string, 
                                 char* user_str_input){
    int string_digit = 1;
    // need length_of_input_string - 1 because the last input can be non digit!
    for (int i = 0; i < length_of_input_string - 1; i++){
        string_digit = isdigit(user_str_input[i]);
        if (string_digit == 0){
            return 1;
        }
    }
    // the - '0' is needed to convert it into a number for comparison
    if (user_str_input[0] == '0' &&
        user_str_input[1] == '0'){
        return 1;
    }
    if (user_str_input[0] - '0' > 1){
        return 1;
    }
    if (user_str_input[1] - '0' > 2 &&
        user_str_input[0] - '0' == 1){
        return 1;
    }
    if (user_str_input[2] - '0' > 5){
        return 1;
    }
    // A == AM and B == PM 
    if (user_str_input[4] != 'A' && user_str_input[4] != 'B'){
        return 1;
    }
    return 0;
}


int verify_clock_time_input_24hr(int length_of_input_string, 
                                 char* user_str_input){
    int string_digit = 1;
    for (int i = 0; i < length_of_input_string; i++){
        string_digit = isdigit(user_str_input[i]);
        if (string_digit == 0){
            return 1;
        }
    }
    // the - '0' is needed to convert it into a number for comparison
    if (user_str_input[0] - '0' > 2){
        return 1;
    }
    if (user_str_input[0] - '0' == 2){
        // check to see if first digit is 2
        // if so then make sure second digit is > 3
        if (user_str_input[1] - '0' > 3){
            return 1;
        }
    }
    if (user_str_input[2] - '0' > 5){
        return 1;
    }
    return 0;
}






void set_24hr_clock_time(int* mode, char user_input, int* time_format, 
                    DateTime* now){
    // keep track of how many user inputs have occured so
    // we can sequentially print our their inputs to the lcd screen.
    static int num_inputs = 0; 
    // get the user input
    // set the time depending on the what time format they selected
    int length_of_input_string = 4;
    char* user_str_input = get_string_input(user_input, 
                                               length_of_input_string);
    // print the user input

    displayCharacters(user_input, 1, num_inputs);
    num_inputs++;
    

    if (user_str_input != NULL){
        if (verify_clock_time_input_24hr(length_of_input_string, 
                                 user_str_input) == 1){
            Serial.println("Invalid string, please try again");
            displayText("Error try again:", 0);
            // clear the input part of the screen 
            displayText("                         ",1);
            num_inputs = 0;
            *mode = 4;
        }
        else {
            num_inputs = 0; // reset it for the next user inputs 
            *mode = 6;
            // the less than or equal will include the \0 character
            Serial.println(user_str_input);
            // <= because we want the 5th \0 character
            // convert chars to ints 
            // *10 because we want the first hour digit and minute digit
            int hour_24 = ((user_str_input[0] - '0') * 10
                         + (user_str_input[1] - '0'));

            int minute = ((user_str_input[2] - '0') * 10
                        + (user_str_input[3] - '0'));

            rtc.adjust(DateTime(
                now->year(),
                now->month(),
                now->day(),
                hour_24,
                minute,
                0
            ));
            Serial.println("Would you like an alarm? (1-yes, 0,-no)");
            displayText("Alarm?", 0);
            displayText("(1-yes, 0-no)", 1);
        }
     }
    free(user_str_input);
}


void set_12hr_clock_time(int* mode, char user_input, int* time_format, 
                    DateTime* now){
    // keep track of how many user inputs have occured so
    // we can sequentially print our their inputs to the lcd screen.
    static int num_inputs = 0; 

    bool is_pm;
    int length_of_input_string = 5;
    char* user_str_input = get_string_input(user_input, 
                                               length_of_input_string);
    // print the user input

    displayCharacters(user_input, 1, num_inputs);
    num_inputs++;
 
    if (user_str_input != NULL){
        if (verify_clock_time_input_12hr(length_of_input_string, 
                                 user_str_input) == 1){
            displayText("Error try again:", 0);
            displayText("Invalid.", 0);
            // clear the input part of the screen 
            displayText("                         ",1);
            num_inputs = 0;        
            *mode = 5;
        }
        else {
            *mode = 6;
            num_inputs = 0;
            // the less than or equal will include the \0 character
            Serial.println(user_str_input);
            // <= because we want the 5th \0 character
            // convert chars to ints 
            // *10 because we want the first hour digit and minute digit
            
            if (user_str_input[4] == 'A') { // AM /
                is_pm = false;
            }
            if (user_str_input[4] == 'B') { // PM /
                is_pm = true;
            }

            int hour_12 = ((user_str_input[0] - '0') * 10
                         + (user_str_input[1] - '0'));
            

            int hour_24 = (hour_12 % 12) + (is_pm ? 12: 0); 

            int minute = ((user_str_input[2] - '0') * 10
                        + (user_str_input[3] - '0'));

            rtc.adjust(DateTime(
                now->year(),
                now->month(),
                now->day(),
                hour_24,
                minute,
                0
            ));
            Serial.println("Would you like an alarm? (1-yes, 0,-no)");
            displayText("Alarm?", 0);
            displayText("(1-yes, 0-no)", 1);
        }
     }
    free(user_str_input);
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
        case 3:
            set_format(user_input, mode, time_format);
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
            set_alarm_time(mode, user_input, alarm_time);
            break;
    }
}



void set_format(char user_input, int* mode, int* time_format){
    if (user_input == '1'){
        Serial.println("Standard time selected!");
        *mode = 5; // now set mode to 4 to ask for alarm
        *time_format = 0;
        Serial.println("Please set the time");
        displayText("Time: A=am B=pm", 0);
        // clear the bottom line for the printchars fxn
        displayText("                ", 1);
    }
    if (user_input == '0'){
        Serial.println("Millitary time selected!");
        *mode = 4;
        *time_format = 1;
        Serial.println("Please set the time");
        displayText("Enter time: ", 0);
        // clear the bottom line for the printchars fxn
        displayText("                ", 1);
    }
}

void show_program_option(char user_input) {

    static int MAX_OPTIONS = 2;
    static int menu_location = 0;
    static int old_menu_location = -1;

    if (user_input == '2') { // scroll up!
        menu_location++;
        if (menu_location > MAX_OPTIONS){
            menu_location = 0;
        }
    }
    else if (user_input == '3'){ // scroll down!
        menu_location--;
        if (menu_location < 0){
            menu_location = MAX_OPTIONS;
        }
    }
    // now look at what needs to be printed


    if (old_menu_location != menu_location){
        if (menu_location == 0){
            displayText("Program Mode", 0);
            displayText("2-UP 3-DWN", 1);
        }
        else if (menu_location == 1){
            displayText("Super clock", 0);
            displayText("Press B ", 1);
        }
        else if (menu_location == 2){
            displayText("Dumb clock", 0);
            displayText("Press A ", 1);
        }
        else {
            displayText("wot?", 0);
        }
        old_menu_location = menu_location;
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
    setupLCD();
}



void loop() {
    char c = keyPad.getChar();
    DateTime now = rtc.now();
    num_time = now.second();

    if (millis() - last_input_time >= input_delay){
        if (c != 'N' && c != 'F'){ // make sure it's not N (no in) or F (fail) 
            Serial.println(c);
            // handle the input given what mode you're in and the input  
            if (c == '#') {
                mode = 1;
                Serial.println("You're in Program mode (1)!");
                displayText("Program Mode", 0);
                displayText("2-UP 3-DWN", 1);
            }
            set_program_mode(c, &mode, &time_format, &alarm_enabled, 
                                &now, alarm_time);
            last_input_time = millis();           
        }
    }

    // do other program stuff 
    if (mode == 2){
        if (num_time != last_time){
            Serial.printf("%02d:%02d:%02d\n",
                  now.hour(),
                  now.minute(),
                  now.second());
            last_time = num_time;
            displayText("67 am", 0);
        }
    }
    
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
        // FIXME: ADD IF statement here to check for alarm time if enabled
    }
}




