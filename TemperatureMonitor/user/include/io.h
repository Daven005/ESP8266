/*
 * output.h
 *
 *  Created on: 22 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_IO_H_
#define USER_INCLUDE_IO_H_

uint8 getOutput(uint8 idx);
void overrideClearOutput(int id);
void setOutput(int id, bool value);
void overrideSetOutput(int id, int value);
void initOutput(void);

#endif /* USER_INCLUDE_IO_H_ */
