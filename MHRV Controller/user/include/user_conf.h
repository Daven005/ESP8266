/*
 * user_config.h -default settings
 */
#include "dht22.h"

#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A9	/* Change this value to load default configurations */
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

#include "IOdefs.h"

// MQTT sensor IDs
#define SENSOR_TEMPERATURE1 0
#define SENSOR_TEMPERATURE2 1
#define SENSOR_HUMIDITY1 2
#define SENSOR_HUMIDITY2 3
#define SENSOR_PIR1 4
#define SENSOR_PIR2 5
#define SENSOR_FAN_PIR_ACTIVE1 6
#define SENSOR_FAN_PIR_ACTIVE2 7
#define SENSOR_FAN 8
#define SENSOR_LIGHT_PIR_ACTIVE1 9
#define SENSOR_LIGHT_PIR_ACTIVE2 10

#define MQTT_HOST			"192.168.1.100" //"broker.mqttdashboard.com"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		512
#define MQTT_KEEPALIVE		120	 /*second*/

#define DEVICE_PREFIX		"Hollies"
#define MQTT_USER			""
#define MQTT_PASS			""

#define STA_SSID "EE-berry"
#include "password.h"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/

#define DEFAULT_SECURITY	0
#define QUEUE_BUFFER_SIZE	4096

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#define NAME_SIZE 33
#define MAP_SIZE 4
#define MAP_TEMP_SIZE 1
#define SETTINGS_SIZE 15
#define MAX_TEMPERATURE_SENSOR 2
#define INPUT_SENSOR_ID_START 10
#define OUTPUT_SENSOR_ID_START 20
#undef INPUTS
#define OUTPUTS 4
#define USE_TIME

#define MAP_TEMP_LAST 0

#define SETTING_HUMIDTY1 0
#define SETTING_HUMIDTY2 1
#define SETTING_TEMPERATURE1 2
#define SETTING_TEMPERATURE2 3
#define SETTING_START_ON 4
#define SETTING_FINISH_ON 5
#define SETTING_FAN_PIR1_ON_TIME 6
#define SETTING_FAN_PIR2_ON_TIME 7
#define SETTING_DHT1 8
#define SETTING_DHT2 9
#define SETTING_PIR_ACTION 10
#define SETTING_LIGHT_PIR1_ON_TIME 11
#define SETTING_LIGHT_PIR2_ON_TIME 12

#define DEFAULT_HUMIDTY1 45
#define DEFAULT_HUMIDTY2 45
#define DEFAULT_TEMPERATURE1 25
#define DEFAULT_TEMPERATURE2 25
#define DEFAULT_START_ON 7
#define DEFAULT_FINISH_ON 22
#define DEFAULT_FAN_PIR1_ON_TIME 20
#define DEFAULT_FAN_PIR2_ON_TIME 20
#define DEFAULT_DHT1 DHT22
#define DEFAULT_DHT2 DHT22
#define DEFAULT_PIR_ACTION 1
#define DEFAULT_LIGHT_PIR1_ON_TIME 5
#define DEFAULT_LIGHT_PIR2_ON_TIME 5

#define SET_MINIMUM 1
#define SET_MAXIMUM 95

#define UPDATES 60

#endif
