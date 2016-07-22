/*
 * user_main.h
 *
 *  Created on: 18 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_USER_MAIN_H_
#define USER_INCLUDE_USER_MAIN_H_

void user_rf_pre_init(void);
void user_init(void);

bool mqttIsConnected(void);
void stopConnection(void);
void startConnection(void);
void _publishDeviceInfo(void);
void publishData(MQTT_Client* client);


#endif /* USER_INCLUDE_USER_MAIN_H_ */
