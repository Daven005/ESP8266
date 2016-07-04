/*
 * BoilerControl.h
 *
 *  Created on: 30 Dec 2015
 *      Author: User
 */

#ifndef INCLUDE_BOILERCONTROL_H_
#define INCLUDE_BOILERCONTROL_H_

void setOutsideTemp(int idx, int val);
void printDHW(void);
void printBCinfo(void);
void checkControl(void);
void initBoilerControl();

#endif /* INCLUDE_BOILERCONTROL_H_ */
