#include <Arduino.h>
#include "alarm.h"
#include <ctype.h> // needed for isdigit fxn for input validation
#include "keypad_input.h"
#include <stdlib.h>
#include "lcd_screen.h"

/*
 * SO APPARENTLY, If you run into issues with arduino not wanting to compile a
 * cpp file and it says its a c file clean our the cash fodler by entering in 
 * rm -rf ~/.cache/arduino/sketches/
 * into the shell to delete the cashes to force it to recompile from scratch
 */

void set_alarm(char user_input, int* mode, bool* alarm_enabled){
    if (user_input == '1'){
        Serial.println("Alarm mode activated");
        Serial.print("Please Enter your alarm time: ");
        displayText("Alarm time:", 0);
        displayText("                         ",1);
        *alarm_enabled = true; 
        *mode = 7; 
    }
    if (user_input == '0'){
        Serial.println("No alarm");
        *alarm_enabled = false;
        *mode = 8;
    }
}

void set_alarm_time(int* mode, char user_input, char* alarm_time, 
                    int* time_format){
    int length_of_input_string = 0;
    char* user_str_input = NULL;


    switch(*time_format){
        case 0: // 12hr time
            length_of_input_string = 5;
            user_str_input = get_string_input(user_input, 
                                              length_of_input_string);

            // keep track of how many user inputs have occured so
            // we can sequentially print our their inputs to the lcd screen.

            static int num_inputs_12hr = 0; 
            bool is_pm;

            // print the user input
            displayCharacters(user_input, 1, num_inputs_12hr);
            num_inputs_12hr++;
         
            if (user_str_input != NULL){
                if (verify_alarm_time_input_12hr(length_of_input_string, 
                                         user_str_input) == 1){
                    displayText("Error try again:", 0);
                    displayText("Invalid.", 0);
                    // clear the input part of the screen 
                    displayText("                         ",1);
                    num_inputs_12hr = 0;        
                    *mode = 7;
                }
                else {
                    *mode = 8;
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
                    
                    // < and not <= to avoid the last char digit
                    alarm_time[0] = (char)((hour_24 / 10) + '0');
                    alarm_time[1] = (char)((hour_24 % 10) + '0');
                    alarm_time[2] = (user_str_input[2]);
                    alarm_time[3] = (user_str_input[3]);
                    Serial.println(alarm_time);
                    num_inputs_12hr = 0;
                    free(user_str_input);
                } 
            break;

        case 1: // 24hr time 
            length_of_input_string = 4;
            user_str_input = get_string_input(user_input, 
                                              length_of_input_string);

            static int num_inputs_24hr = 0;

            displayCharacters(user_input, 1, num_inputs_24hr);
            num_inputs_24hr++;
    
            if (user_str_input != NULL){
                if (verify_alarm_time_input_24hr(length_of_input_string, 
                                                 user_str_input) == 1){
                    Serial.println("Invalid string, please try again");
                    displayText("Error try again:", 0);
                    // clear the input part of the screen 
                    displayText("                         ",1);
                    num_inputs_24hr = 0;
                    *mode = 7;
                } 
                else {
                    *mode = 8;
                    // the less than or equal will include the \0 character
                    Serial.println(user_str_input);
                    // <= because we want the 5th \0 character
                    for (int i = 0; i <= length_of_input_string; i++){
                        alarm_time[i] = user_str_input[i];
                    }
                    num_inputs_24hr = 0;
                    free(user_str_input);
                }
            }
            break;
        }
    }
}
int verify_alarm_time_seconds(int length_of_input_string, 
                                 char* user_str_input){
    int string_digit = 1;
    for (int i = 0; i < length_of_input_string; i++){
        string_digit = isdigit(user_str_input[i]);
        if (string_digit == 0){
            return 1;
        }
    }
    return 0;
}

int verify_alarm_time_input_24hr(int length_of_input_string, 
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


int verify_alarm_time_input_12hr(int length_of_input_string, 
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




