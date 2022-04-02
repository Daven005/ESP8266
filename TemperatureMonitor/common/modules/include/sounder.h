/*
 * sounder.h
 *
 *  Created on: 8 Oct 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_SOUNDER_H_
#define USER_INCLUDE_SOUNDER_H_
#include "IOdefs.h"

void initSounder(void);
void sounderClear(void);
void sounderAlarm(uint8);
bool sounderActive(void);
void overrideSetSounder(override_t o);

#endif /* USER_INCLUDE_SOUNDER_H_ */
