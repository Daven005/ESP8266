/*
 * flash.h
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */

#ifndef COMMON_DRIVER_INCLUDE_FLASH_H_
#define COMMON_DRIVER_INCLUDE_FLASH_H_
typedef void (*flashCb_t)(void);

#ifdef LED
void stopFlash(void);
void startFlash(int count, unsigned int onTime, unsigned int offTime);
void startMultiFlash(int count, uint8 flashCount, unsigned int flashTime, unsigned int offTime);
void startMultiFlashCb(int count, uint8 flashCount, unsigned int flashTime, unsigned int offTime,
		flashCb_t cb);
#endif
#ifdef ACTION_LED
void stopActionFlash(void);
void startActionFlash(int count, unsigned int onTime, unsigned int offTime);
void startActionMultiFlash(int count, uint8 flashCount, unsigned int flashTime, unsigned int offTime);
void startActionMultiFlashCb(int count, uint8 flashCount, unsigned int flashTime, unsigned int offTime,
		flashCb_t cb);
#endif

#endif /* COMMON_DRIVER_INCLUDE_FLASH_H_ */
