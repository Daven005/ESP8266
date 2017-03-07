#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A9	/* Change this value to load default configurations */
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

#define MQTT_HOST			"192.168.1.100" //"broker.mqttdashboard.com"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		1024
#define MQTT_KEEPALIVE		120	 /*second*/

#define DEVICE_PREFIX		"Hollies"
#define MQTT_USER			""
#define MQTT_PASS			""

#define STA_SSID "EE-berry"
#define STA_PASS "fog-phone-deep"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/

#define DEFAULT_SECURITY	0
#define QUEUE_BUFFER_SIZE		 		4096

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#include "IOdefs.h"

#define USE_FLOWS 1
#define MAP_TEMP_SIZE 0
#define MAX_TEMPERATURE_SENSOR 0
#define MAP_TEMP_LAST 0

#define NAME_SIZE 32
#define MAP_SIZE 4
#define SETTINGS_SIZE 10
#define MAX_OUTPUT 2

#define INPUT_SENSOR_ID_START 10

#define SET_PUMP_ON 0
#define SET_PUMP_OFF 1
#define SET_FLOW_TIMER 2
#define SET_FLOW_COUNT_PER_LITRE 3
#define SET_LOW_PRESSURE_WARNING 4
#define SET_LOW_LEVEL_WARNING 5
#define SET_MAX_PUMP_ON_WARNING 6
#define SET_MAX_PUMP_ON_ERROR 7
#define SET_NO_FLOW_AUTO_ERROR 8

#define DEFAULT_PUMP_ON 100
#define DEFAULT_PUMP_OFF 200
#define DEFAULT_FLOW_TIMER 10
#define DEFAULT_FLOW_COUNT_PER_LITRE 450
#define DEFAULT_LOW_PRESSURE_WARNING 110
#define DEFAULT_LOW_LEVEL_WARNING 110
#define DEFAULT_MAX_PUMP_ON_WARNING 1800 // 3 mins
#define DEFAULT_MAX_PUMP_ON_ERROR 3000 // 5 Min
#define DEFAULT_NO_FLOW_AUTO 10

#define SET_MINIMUM 0
#define SET_MAXIMUM 10000

#define UPDATES 60
#define INPUTS 0

#endif
