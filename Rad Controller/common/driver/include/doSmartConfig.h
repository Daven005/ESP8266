/*
 * doSmartConfig.h
 *
 *  Created on: 12 Jul 2016
 *      Author: User
 */

#ifndef COMMON_DRIVER_INCLUDE_DOSMARTCONFIG_H_
#define COMMON_DRIVER_INCLUDE_DOSMARTCONFIG_H_
#include "smartConfig.h"
enum SmartConfigAction {
	SC_CHECK, SC_HAS_STOPPED, SC_TOGGLE
};
void smartConfig_done(sc_status status, void *pdata);
bool checkSmartConfig(enum SmartConfigAction action);

#endif /* COMMON_DRIVER_INCLUDE_DOSMARTCONFIG_H_ */
