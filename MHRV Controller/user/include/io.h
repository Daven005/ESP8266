/*
 * io.h
 *
 *  Created on: 29 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_IO_H_
#define USER_INCLUDE_IO_H_

enum led_t {
	DARK, RED, GREEN, YELLOW
};
enum speedSelect {
	STOP, SLOW, FAST
};

enum pir_t { PIR1=1, PIR2=2 };


void initOutputs(void);
void initInputs(void);
void setOutput(uint8 id, bool set);
void forceOutput(uint8 id, bool set);
void clrOverride(uint8 id);
void setLED(enum led_t led);
enum speedSelect getSpeed(void);
void setSpeed(enum speedSelect speed);
void printOutputs(void);
bool checkPirActive(enum pir_t actionPir);
void clearPirActive(enum pir_t pir);
void setPirActive(enum pir_t pir);
bool pirState(enum pir_t pir);

#endif /* USER_INCLUDE_IO_H_ */
