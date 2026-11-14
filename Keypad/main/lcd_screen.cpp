#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>
#include "lcd_screen.h"

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

void displayClear(){
    lcd.clear();
}

// REUSABLE FUNCTION: Display text on LCD
void displayText(String text, int row) {
    lcd.setCursor(0, row);
    lcd.print(text);

    // Fill the remaining characters with spaces
    for (int i = text.length(); i < 16; i++) {
        lcd.print(' ');
    }
}

// REUSABLE FUNCTION: Display text on LCD
void displayCharacters(char character, int row, int col) {
    lcd.setCursor(col, row);
    lcd.print(character);
}
// REUSABLE FUNCTION: Display standard or military time
void displayTime(int hour, int minute, int second, int time_format,
                                                   char period) {

    char timeText[17];

    if (time_format == 1) {

        displayText("Military Time", 0);

        snprintf(timeText, sizeof(timeText),
                         "%02d:%02d:%02d", hour, minute, second);

    } 
    if (time_format == 0){
        
        displayText("Standard Time", 0);

        if (period == 'A'){
            snprintf(timeText, sizeof(timeText),
                         "%02d:%02d:%02d %s", hour, minute, second, "AM");
        }
        if (period == 'B'){
            snprintf(timeText, sizeof(timeText),
                         "%02d:%02d:%02d %s", hour, minute, second, "PM");
        }
    }
    displayText(timeText, 1);
}

void setupLCD() {

  lcd.init();
  lcd.backlight();

}


