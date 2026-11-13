#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Feather ESP32-S3 BOOT button
const int buttonPin = 0;

// Time format
bool militaryTime = false;
bool lastButton = HIGH;

// Example starting time: 3:34 PM
int hour = 15;
int minute = 34;
int second = 0;

unsigned long previousMillis = 0;

// REUSABLE FUNCTION: Display text on LCD
void displayText(String text, int row) {
  lcd.setCursor(0, row);
  lcd.print("                ");
  lcd.setCursor(0, row);
  lcd.print(text);
}

// REUSABLE FUNCTION: Display standard or military time
void displayTime(int h, int m) {

  char timeText[17];

  if (militaryTime) {

    displayText("Military Time", 0);

    snprintf(timeText, sizeof(timeText),
             "%02d:%02d", h, m);

  } else {

    displayText("Standard Time", 0);

    int standardHour = h % 12;
    if (standardHour == 0) standardHour = 12;

    snprintf(timeText, sizeof(timeText),
             "%02d:%02d %s",
             standardHour, m,
             h >= 12 ? "PM" : "AM");
  }

  displayText(timeText, 1);
}

void setup() {
  Wire.begin();

  lcd.init();
  lcd.backlight();

  pinMode(buttonPin, INPUT_PULLUP);

  previousMillis = millis();

  displayTime(hour, minute);
}

void loop() {

  // Detect button press
  bool currentButton = digitalRead(buttonPin);

  if (lastButton == HIGH && currentButton == LOW) {
    militaryTime = !militaryTime;
    displayTime(hour, minute);
    delay(200);
  }

  lastButton = currentButton;

  // Simulate a running clock
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= 1000) {
    previousMillis += 1000;

    second++;

    if (second >= 60) {
      second = 0;
      minute++;
    }

    if (minute >= 60) {
      minute = 0;
      hour++;
    }

    if (hour >= 24) {
      hour = 0;
    }

    displayTime(hour, minute);
  }
}