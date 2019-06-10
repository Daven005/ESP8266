/******************************************************************************
* Copyright 2013-2014 Espressif Systems (Wuxi)
*
* FileName: hw_timer.c
*
* Description: hw_timer driver
*
* Modification history:
*     2014/5/1, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "debug.h"
#include "hw_timer.h"

#define US_TO_RTC_TIMER_TICKS(t)          \
    ((t) ?                                   \
     (((t) > 0x35A) ?                   \
      (((t)>>2) * ((APB_CLK_FREQ>>4)/250000) + ((t)&0x3) * ((APB_CLK_FREQ>>4)/1000000))  :    \
      (((t) *(APB_CLK_FREQ>>4)) / 1000000)) :    \
     0)

#define FRC1_ENABLE_TIMER  BIT7
#define FRC1_AUTO_LOAD  BIT6

//TIMER PREDIVED MODE
typedef enum {
    DIVDED_BY_1 = 0,		//timer clock
    DIVDED_BY_16 = 4,	//divided by 16
    DIVDED_BY_256 = 8,	//divided by 256
} TIMER_PREDIVED_MODE;

typedef enum {			//timer interrupt mode
    TM_LEVEL_INT = 1,	// level interrupt
    TM_EDGE_INT   = 0,	//edge interrupt
} TIMER_INT_MODE;

static void (* user_hw_timer_cb)(void) = NULL;

void  hw_timer_set_func(void (* user_hw_timer_cb_set)(void)) {
    user_hw_timer_cb = user_hw_timer_cb_set;
}

static void  hw_timer_isr_cb(void * x) {
    if (user_hw_timer_cb != NULL) {
        (*(user_hw_timer_cb))();
    }
}

void ICACHE_FLASH_ATTR hw_timer_restart(uint32_t val) {
    RTC_REG_WRITE(FRC1_LOAD_ADDRESS, US_TO_RTC_TIMER_TICKS(val));
    RTC_REG_WRITE(FRC1_CTRL_ADDRESS, FRC1_AUTO_LOAD | DIVDED_BY_16 | FRC1_ENABLE_TIMER | TM_EDGE_INT);
    RTC_CLR_REG_MASK(FRC1_INT_ADDRESS, FRC1_INT_CLR_MASK);

    ETS_FRC_TIMER1_INTR_ATTACH(hw_timer_isr_cb, NULL);
    TM1_EDGE_INT_ENABLE();
    ETS_FRC1_INTR_ENABLE();
}

void ICACHE_FLASH_ATTR hw_timer_disable(void) {
	ETS_FRC1_INTR_DISABLE();
	TM1_EDGE_INT_DISABLE();
	RTC_REG_WRITE(FRC1_CTRL_ADDRESS, 0);
}
