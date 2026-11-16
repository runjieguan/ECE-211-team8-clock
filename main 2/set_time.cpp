#include <Arduino.h>
#include "set_time.h"
#include "lcd_screen.h"
#include "keypad_input.h"
#include <stdlib.h>


RTC_DS3231 rtc; // initalize the rtc we're using 

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



void set_format(char user_input, int* mode, int* time_format, 
                bool skip_to_mode_8){
    if (user_input == '1' && skip_to_mode_8 == false){
        Serial.println("Standard time selected!");
        *mode = 5; // now set mode to 4 to ask for alarm
        *time_format = 0;
        displayText("Time: A=am B=pm", 0);
        // clear the bottom line for the printchars fxn
        displayText("                ", 1);
    }
    if (user_input == '1' && skip_to_mode_8 == true){
        Serial.println("Standard time selected!");
        *mode = 8; 
        *time_format = 0;
        displayText("Time: A=am B=pm", 0);
        // clear the bottom line for the printchars fxn
        displayText("                ", 1);
    }
    if (user_input == '0' && skip_to_mode_8 == false){
        Serial.println("Millitary time selected!");
        *mode = 4;
        *time_format = 1;
        Serial.println("Please set the time");
        displayText("Enter time: ", 0);
        // clear the bottom line for the printchars fxn
        displayText("                ", 1);
    }
    if (user_input == '0' && skip_to_mode_8 == true){
        Serial.println("Millitary time selected!");
        *mode = 8;
        *time_format = 1;
    }

}


