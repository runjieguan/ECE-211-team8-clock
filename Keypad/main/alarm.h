#ifndef ALARM // overarching ifdef guard here for the whole file!
#define ALARM 

void set_alarm(char user_input, int* mode, bool* alarm_enabled);
int verify_alarm_time_input_24hr(int length_of_input_string, 
                                 char* user_str_input);
void set_alarm_time(int* mode, char user_char_input, char* alarm_time);
int verify_alarm_time_seconds(int length_of_input_string, 
                                 char* user_str_input);
#endif 


