#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD address, columns, rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  // SDA = GPIO 8, SCL = GPIO 7
  Wire.begin(8, 7);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // First row
  lcd.setCursor(0, 0);
  lcd.print("ECE 211 Project");

  // Second row
  lcd.setCursor(0, 1);
  lcd.print("LCD Working!");
}

void loop() {
}