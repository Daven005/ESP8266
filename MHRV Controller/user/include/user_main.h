/*
 * user_main.h
 *
 *  Created on: 30 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_USER_MAIN_H_
#define USER_INCLUDE_USER_MAIN_H_

bool mqttIsConnected(void);
void stopConnection(void);
void startConnection(void);
void user_init(void);
void publishSensorData(uint8 sensor, char *type, int info);
void _publishDeviceInfo(void);
void publishData(void);
void setExternalTemperature(char* dataBuf);
void setTime(char* dataBuf);

#endif /* USER_INCLUDE_USER_MAIN_H_ */
