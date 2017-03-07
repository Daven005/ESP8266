/*
 * io.h
 *
 *  Created on: 7 Mar 2017
 *      Author: User
 */

#ifndef USER_INCLUDE_IO_H_
#define USER_INCLUDE_IO_H_
#include "pump.h"

uint16 getCurrentPressure(void);
void overrideSetPressure(uint16 p);
void overrideClearPressure(void);
void updatePressure(void);

pumpState_t pumpState(void);
void overrideSetPump(char* p);
void overrideClearPump(void);
void pumpState_OffManual(void);
void pumpState_OnManual(void);
void pumpState_OffAuto(void);
void pumpState_OnAuto(void);

#endif /* USER_INCLUDE_IO_H_ */
