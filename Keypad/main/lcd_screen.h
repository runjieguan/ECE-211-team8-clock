#ifndef LCD_SCREEN // overarching ifdef guard here for the whole file!
#define LCD_SCREEN 


void displayText(String text, int row); 
void displayCharacters(char character, int row, int col);
void displayTime(int hour, int minute, int second, int time_format,
                                                   char period); 
void setupLCD();
void displayClear();


#endif 

