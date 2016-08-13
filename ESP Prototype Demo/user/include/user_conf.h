#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A2	/* Change this value to load default configurations */
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

#define SLEEP_MODE 0
#undef USE_TEMPERATURE
#define USE_WIFI_SCAN
#define LED 5
#ifdef LED
#define SWITCH 0 			// Switch requires LED (for feedback)
#ifdef SWITCH
#define USE_SMART_CONFIG 	// Requires Switch && LED
#define USE_WEB_CONFIG		// Requires Switch && LED
#endif
#endif

#define MAX_DS18B20_SENSOR 10
#define MAX_TEMPERATURE_SENSOR 15
#define INPUT_SENSOR_ID_START 10
#define OUTPUT_SENSOR_ID_START 20

#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define DS18B20_PIN		2


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
#define QUEUE_BUFFER_SIZE		 		2048

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#define NAME_SIZE 32
#define MAP_SIZE 1
#define MAP_TEMP_SIZE 1
#define SETTINGS_SIZE 20
#define MAX_OUTPUT 2
#define SET_MINIMUM 0
#define SET_MAXIMUM 95


#define UPDATES 60
#define INPUTS 0

#endif
