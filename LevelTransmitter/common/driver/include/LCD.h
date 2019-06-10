// Default definitions - put in IOdefs.h

//#define LCD_Light 15
//#define LCD_SCE 3
//#define LCD_clk 14
//#define LCD_Data 12
//#define LCD_D_C 13
//#define LCD_RST 2

extern void clearLcd(void);
extern uint8 showString(uint8 xPosn, uint8 yPosn, char *s);
extern void lcdInit(void);
extern void lcdReset(void);
