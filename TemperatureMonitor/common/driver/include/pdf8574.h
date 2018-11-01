/*
 * pdf8574.h
 *
 *  Created on: 15 Oct 2018
 *      Author: User
 */

#ifndef COMMON_DRIVER_INCLUDE_PDF8574_H_
#define COMMON_DRIVER_INCLUDE_PDF8574_H_
#include <os_type.h>

#define PDF8574_ADDR 0x20

void initExpander(void);
void setExpanderOutput(uint8 op);
uint8 getExpanderOutput(void);

#endif /* COMMON_DRIVER_INCLUDE_PDF8574_H_ */
