/*
 * user_main.h
 *
 *  Created on: 30 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_USER_MAIN_H_
#define USER_INCLUDE_USER_MAIN_H_

#include "time.h"
bool mqttIsConnected(void);
void stopConnection(void);
void startConnection(void);
void user_init(void);
void publishSensorData(uint8 sensor, char *type, char *info);
void _publishDeviceInfo(void);
void publishData(uint32);
void setExternalTemperature(char* dataBuf);
void setTime(time_t t);
void resetTransmitTimer(void);

#endif /* USER_INCLUDE_USER_MAIN_H_ */
