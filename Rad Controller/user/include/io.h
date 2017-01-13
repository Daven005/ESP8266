/*
 * io.h
 *
 *  Created on: 22 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_IO_H_
#define USER_INCLUDE_IO_H_

enum override_t { OVERRIDE_OFF, OVERRIDE_ON, NO_OVERRIDE  };
uint8 getOutput(uint8 idx);
void overrideClearOutput(int id);
void overrideSetOutput(int id, int value);
void initOutput(void);
void setLevelTwoOverride(int id, enum override_t or);

#endif /* USER_INCLUDE_IO_H_ */
