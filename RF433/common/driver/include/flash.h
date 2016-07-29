/*
 * flash.h
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */

#ifndef COMMON_DRIVER_INCLUDE_FLASH_H_
#define COMMON_DRIVER_INCLUDE_FLASH_H_

#ifdef LED
void stopFlash(void);
void startFlash(int count, unsigned int onTime, unsigned int offTime);
#endif
#ifdef ACTION_LED
void stopActionFlash(void);
void startActionFlash(int count, unsigned int onTime, unsigned int offTime);
#endif

#endif /* COMMON_DRIVER_INCLUDE_FLASH_H_ */
