#include <Arduino.h>
#include "alarm.h"
#include <ctype.h> // needed for isdigit fxn for input validation
#include "keypad_input.h"
#include <stdlib.h>

/*
 * SO APPARENTLY, If you run into issues with arduino not wanting to compile a
 * cpp file and it says its a c file clean our the cash fodler by entering in 
 * rm -rf ~/.cache/arduino/sketches/
 * into the shell to delete the cashes to force it to recompile from scratch
 */

void set_alarm_time(int* mode, char user_char_input, char* alarm_time){
    int length_of_input_string = 4;
    char* user_str_input = get_string_input(user_char_input, 
                                               length_of_input_string);
    if (user_str_input != NULL){
        if (verify_alarm_time_input_24hr(length_of_input_string,
                                        user_str_input) == 1){
            Serial.println("Invalid string, please try again");
            *mode = 5;
        } 
        else {
            *mode = 6;
            // the less than or equal will include the \0 character
            Serial.println(user_str_input);
            // <= because we want the 5th \0 character
            for (int i = 0; i <= length_of_input_string; i++){
                alarm_time[i] = user_str_input[i];
            }
        }
        free(user_str_input);
    }
}

int verify_alarm_time_seconds(int length_of_input_string, 
                                 char* user_str_input){
    int string_digit = 1;
    for (int i = 0; i < length_of_input_string; i++){
        string_digit = isdigit((unsigned char)user_str_input[i]);
        if (string_digit == 0){
            return 1;
        }
    }
    return 0;
}

int verify_alarm_time_input_24hr(int length_of_input_string, 
                                 char* user_str_input){
    if (length_of_input_string != 4 || user_str_input == NULL ||
        verify_alarm_time_seconds(length_of_input_string, user_str_input) == 1){
        return 1;
    }

    int hour = 10 * (user_str_input[0] - '0') + (user_str_input[1] - '0');
    int minute = 10 * (user_str_input[2] - '0') + (user_str_input[3] - '0');
    return hour > 23 || minute > 59;
}


void set_alarm(char user_input, int* mode, bool* alarm_enabled){
    if (user_input == '1'){
        Serial.println("Alarm mode activated");
        Serial.print("Please Enter your alarm time (HHMM): ");
        *alarm_enabled = true; 
        *mode = 5; 
    }
    if (user_input == '0'){
        Serial.println("No alarm");
        *alarm_enabled = false;
        *mode = 6;
    }
}



