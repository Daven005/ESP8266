/*
 * switch.h
 *
 *  Created on: 18 Jul 2016
 *      Author: User
 */

#ifndef DRIVER_INCLUDE_SWITCH_H_
#define DRIVER_INCLUDE_SWITCH_H_

typedef void (*SwitchActionCb_t)(int);
void initSwitch(SwitchActionCb_t cb);

#endif /* DRIVER_INCLUDE_SWITCH_H_ */
