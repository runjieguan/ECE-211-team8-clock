#ifndef KEYPAD_INPUT // overarching ifdef guard here for the whole file!
#define KEYPAD_INPUT

// -----------------
#ifdef __cplusplus  // This is for the linking stage of compilation.  The ardunio uses the g++ linker which is c++ based. 
                    // So these commands prevent our c compiler from looking at
                    // these c++ commands while keeping them around for the linking stage which is c++ based.
extern "C" {
#endif
// -----------------

char* get_string_input(char user_input, int length_of_input);

// -----------------
#ifdef __cplusplus
}
#endif
// -----------------

#endif 





