#include <Arduino.h>
#include "keypad_input.h"

// Keep partial input accessible so # can cancel it.
static char* input_array = NULL;
static int string_iteration_count = 0;

void reset_string_input(void) {
    free(input_array);
    input_array = NULL;
    string_iteration_count = 0;
}

char* get_string_input(char user_input, int length_of_input) {
    // Allocate memory when starting a new input
    if (input_array == NULL) {
        input_array = (char* ) malloc((length_of_input + 1) * sizeof(char));

        if (input_array == NULL) {
            return NULL; // malloc failed
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

    return NULL; // Still collecting characters
}

char keypad_take_press(char sample, uint32_t now_ms) {
    static char candidate = 'N';
    static uint32_t changed_at = 0;
    static bool released = true;

    if (sample != candidate) {
        candidate = sample;
        changed_at = now_ms;
    }
    if (now_ms - changed_at < 30U) {
        return '\0';
    }
    if (sample == 'N') {
        released = true;
    } else if (released && ((sample >= '0' && sample <= '9') ||
                             (sample >= 'A' && sample <= 'D') || sample == '*' || sample == '#')) {
        released = false;
        return sample;
    }
    return '\0';
}

int keypad_menu_mode(char user_input, int mode) {
    if (user_input == '#') {
        return KEYPAD_MENU;
    }
    if (mode == KEYPAD_MENU) {
        if (user_input == '1') {
            return KEYPAD_CLOCK;
        }
        if (user_input == '2') {
            return KEYPAD_STOPWATCH;
        }
    }
    return mode;
}

static bool stopwatch_running = false;
static uint64_t stopwatch_started_at_us = 0;
static uint64_t stopwatch_elapsed_us = 0;

bool keypad_stopwatch_key(char user_input, uint64_t now_us) {
    if (user_input == 'A') {
        if (stopwatch_running) {
            stopwatch_elapsed_us += now_us - stopwatch_started_at_us;
        } else {
            stopwatch_started_at_us = now_us;
        }
        stopwatch_running = !stopwatch_running;
    } else if (user_input == 'B') {
        stopwatch_running = false;
        stopwatch_elapsed_us = 0;
    } else {
        return false;
    }
    return true;
}

uint64_t keypad_stopwatch_centiseconds(uint64_t now_us) {
    uint64_t elapsed_us = stopwatch_elapsed_us;
    if (stopwatch_running) {
        elapsed_us += now_us - stopwatch_started_at_us;
    }
    return elapsed_us / 10000U;
}

