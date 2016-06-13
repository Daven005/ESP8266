/*
 * lt2400.h
 *
 *  Created on: 1 Mar 2016
 *      Author: User
 */

#ifndef MODULES_INCLUDE_LT2400_H_
#define MODULES_INCLUDE_LT2400_H_

#define LT2400_CS 13
#define LT2400_SDO 12
#define LT2400_SCK 14

void lt2400_IO_Init(void);
bool lt2400_ready(void);
int32 lt2400_read(void);
uint8 lt2400_status(void);

#endif /* MODULES_INCLUDE_LT2400_H_ */
