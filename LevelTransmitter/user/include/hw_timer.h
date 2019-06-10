/*
 * hw_timer.h
 *
 *  Created on: 9 Sep 2016
 *      Author: User
 */

#ifndef DRIVER_INCLUDE_HW_TIMER_H_
#define DRIVER_INCLUDE_HW_TIMER_H_

typedef enum {
    FRC1_SOURCE = 0,
    NMI_SOURCE = 1,
} FRC1_TIMER_SOURCE_TYPE;

void  hw_timer_disable(void);
void  hw_timer_restart(uint32_t val);
void  hw_timer_set_func(void (* user_hw_timer_cb_set)(void));

#endif /* DRIVER_INCLUDE_HW_TIMER_H_ */
