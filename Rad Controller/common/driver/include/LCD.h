// Default definitions - put in IOdefs.h

//#define LCD_Light 15
//#define LCD_SCE 3
//#define LCD_clk 14
//#define LCD_Data 12
//#define LCD_D_C 13
//#define LCD_RST 2

void clearLcd(void);
void showChar(const char *c,  uint8 charCol, uint8 charRow);
uint8 showString(uint8 xPosn, uint8 yPosn, char *s);
uint8 showInvertedString(uint8 xPosn, uint8 yPosn, char *s);
uint8 showLargeNumString(uint8 xPosn, uint8 yPosn, char *s);
uint8 showLargeNum(uint8 xPosn, uint8 yPosn, int val);
void lcdInit(void);
void lcdLight(bool on);
// void lcdReset(void);
