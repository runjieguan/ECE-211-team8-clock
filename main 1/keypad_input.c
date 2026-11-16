#include <Arduino.h>
#include "keypad_input.h"



char* get_string_input(char user_input, int length_of_input) {

  // static variables have a permanent location in memory and don't get
  // recreated everytime a fxn calls them
  static char* input_array = NULL;
  static int string_iteration_count = 0;

  // Allocate memory when starting a new input
  if (input_array == NULL) {
    input_array = (char*)malloc((length_of_input + 1) * sizeof(char));

    if (input_array == NULL) {
      return NULL;  // malloc failed
    }
  }

  // Store the incoming character
  input_array[string_iteration_count] = user_input;
  string_iteration_count++;

  // Check whether the string is complete
  if (string_iteration_count == length_of_input) {

    input_array[string_iteration_count] = '\0';

    // Reset for the next input
    string_iteration_count = 0;

    // Transfer ownership to the calling function
    char* completed_input = input_array;
    input_array = NULL;

    return completed_input;
  }

  return NULL;  // Still collecting characters
}
