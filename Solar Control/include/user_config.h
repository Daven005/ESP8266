#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A5	/* Change this value to load default configurations */
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

#define MQTT_HOST			"192.168.1.100" //"broker.mqttdashboard.com"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		512
#define MQTT_KEEPALIVE		120	 /*second*/

#define MQTT_CLIENT_ID		"Hollies%lx"
#define MQTT_USER			""
#define MQTT_PASS			""

#define STA_SSID "EE-berry"
#define STA_PASS "fog-phone-deep"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/

#define DEFAULT_SECURITY	0
#define QUEUE_BUFFER_SIZE		 		2048

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#define USE_PT100 0

#define NAME_SIZE 32
#define MAP_SIZE 4
#define SETTINGS_SIZE 15
#define MAP_TEMP_SIZE 4

#define MAX_OUTPUT 1
#define SET_MINIMUM 0
#define SET_MAXIMUM 60000
#define OUTPUT_SENSOR_ID_START 10

#define SET_T0 0
#define SET_T0_READING 1
#define SET_T1 2
#define SET_T1_READING 3
#define SET_FLOW_TIMER 4
#define SET_FLOW_COUNT_PER_LITRE 5

#define DEFAULT_T0_READING 190
#define DEFAULT_T1_READING 320
#define DEFAULT_T0 0
#define DEFAULT_T1 100
#define DEFAULT_FLOW_TIMER 10
#define DEFAULT_FLOW_COUNT_PER_LITRE 450

#define MAP_TEMP_PANEL 0
#define MAP_TEMP_TS_BOTTOM 1
#define MAP_TEMP_SUPPLY 2
#define MAP_TEMP_RETURN 3

#define OP_PUMP 0

#define UPDATES 60
#define INPUTS 0

#endif
