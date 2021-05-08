/*
 * io.h
 *
 *  Created on: 17 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_IO_H_
#define INCLUDE_IO_H_

bool input(uint8 id);
bool outputState(uint8 id);

void checkSetOutput(uint8 op, uint8 set);
void overrideSetOutput(uint8 op, uint8 set);
void overrideClearOutput(uint8 id);
void overrideSetInput(uint8 op, uint8 set);
void overrideClearInput(uint8 id);
void checkInputs(bool);
void checkOutputs(void);
void clearOutputs(void);

void printOutput(uint8 op);
void printInput(uint8 op);
uint8 readOLAT(void);
uint8 readGPIO(void);
void printIOreg(void);
void initIO(void);

#endif /* INCLUDE_IO_H_ */
