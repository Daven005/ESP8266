/*
 * LevelSignal.h
 *
 *  Created on: 12 Sep 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_LEVELSIGNAL_H_
#define USER_INCLUDE_LEVELSIGNAL_H_

void initLevelSignal(void);
uint16 getLevel(void);
void isrLevelSignal(uint32 gpio_status);

#endif /* USER_INCLUDE_LEVELSIGNAL_H_ */
