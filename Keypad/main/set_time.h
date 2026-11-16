#ifndef SET_TIME // overarching ifdef guard here for the whole file!
#define SET_TIME

#include <RTClib.h>

extern RTC_DS3231 rtc; // initalize the rtc we're using 

int verify_clock_time_input_12hr(int length_of_input_string, 
                                 char* user_str_input);
int verify_clock_time_input_24hr(int length_of_input_string, 
                                 char* user_str_input);
void set_24hr_clock_time(int* mode, char user_input, int* time_format, 
                        DateTime* now);
void set_12hr_clock_time(int* mode, char user_input, int* time_format, 
                        DateTime* now);
void set_format(char user_input, int* mode, int* time_format, 
                bool skip_to_mode_8);


#endif 


