#ifndef KEYPAD_INPUT // overarching ifdef guard here for the whole file!
#define KEYPAD_INPUT

#include <stdbool.h>
#include <stdint.h>

// -----------------
#ifdef __cplusplus  // This is for the linking stage of compilation.  The ardunio uses the g++ linker which is c++ based. 
                    // So these commands prevent our c compiler from looking at
                    // these c++ commands while keeping them around for the linking stage which is c++ based.
extern "C" {
#endif
// -----------------

char* get_string_input(char user_input, int length_of_input);
void reset_string_input(void);

enum { KEYPAD_MENU = 1, KEYPAD_CLOCK = 2, KEYPAD_STOPWATCH = 7 };
char keypad_take_press(char sample, uint32_t now_ms);
int keypad_menu_mode(char user_input, int mode);
bool keypad_stopwatch_key(char user_input, uint64_t now_us);
uint64_t keypad_stopwatch_centiseconds(uint64_t now_us);

// -----------------
#ifdef __cplusplus
}
#endif
// -----------------

#endif 





