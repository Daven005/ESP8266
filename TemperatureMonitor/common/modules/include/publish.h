/*
 * publish.h
 *
 *  Created on: 11 Jul 2016
 *      Author: User
 */

#ifndef USER_PUBLISH_H_
#define USER_PUBLISH_H_

#include "mqtt.h"
void publish(char *name, char *topicTemplate, char *data);
void publishInt(char *type, char * sensor, int16_t value);
void publishAllTemperatures(void);
void publishTemperature(int idx);
void publishAnalogue(uint16 val);
void publishError(uint8 err, int info);
void publishAlarm(uint8 alarm, int info);
void publishDeviceReset(char *version, int lastAction);
void publishDeviceInfo(char *version, char *mode, uint8 wifiChannel,
		uint16 wifiAttempts, char *bestSSID, uint16 vcc);
void publishMapping(void);
void publishOutput(uint8 idx, uint8 val);
void publishInput(uint8 idx, uint8 val);
void initPublish(MQTT_Client* client);
void publishOverride(void);

#endif /* USER_PUBLISH_H_ */
