/*
 * io.h
 *
 *  Created on: 17 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_OVERRIDEIO_H_
#define INCLUDE_OVERRIDEIO_H_

typedef enum { OR_NOT_SET, OR_OFF, OR_ON, OR_INVALID } override_t;
typedef enum { PUMP_UNKNOWN, PUMP_OFF_NORMAL, PUMP_ON_NORMAL, PUMP_OFF_OVERRIDE, PUMP_ON_OVERRIDE } pumpState_t;

pumpState_t pumpState(void);
void clearPumpOverride(void);
bool outputState(uint8 id);

void checkSetOutput(uint8 op, uint8 set);
void overrideSetOutput(uint8 op, uint8 set);
void overrideClearOutput(uint8 id);
void checkOutputs(void);
override_t getOverride(uint8);

void printOutput(uint8 op);

#endif /* INCLUDE_OVERRIDEIO_H_ */
