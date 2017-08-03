/*
 * rtc.h
 *
 *  Created on: 25 Jul 2017
 *      Author: User
 */

#ifndef USER_INCLUDE_RTC_H_
#define USER_INCLUDE_RTC_H_

#include "time.h"

time_t getTime(void);
void setTime(time_t t);
bool isTimeValid(void);
void printTime(void);

#endif /* USER_INCLUDE_RTC_H_ */
