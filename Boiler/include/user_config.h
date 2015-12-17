#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A2	/* Change this value to load default configurations */
#define CFG_LOCATION	0x3C	/* Please don't change or if you know what you doing */
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

#define NAME_SIZE 32
#define MAP_SIZE 4
#define SETTINGS_SIZE 10
#define MAX_OUTPUT 2

#define SETTING_DHW_START_BOILER 0
#define SETTING_DHW_STOP_BOILER 1
#define SETTING_WB_IS_ON_TEMP 2
#define SETTING_CH_START_BOILER 3
#define SETTING_DHW_USE_ALL_HEAT 4
#define SETTING_RADS_START_BOILER 5
#define SETTING_DHW_ON_HOUR 8
#define SETTING_DHW_OFF_HOUR 9

#define SET_MINIMUM 0
#define SET_MAXIMUM 90

#define MAP_TEMP_TS_TOP 0
#define MAP_TEMP_TS_MIDDLE 1
#define MAP_TEMP_TS_BOTTOM 2
#define MAP_WB_FLOW_TEMP 3

#define IP_CH_ON 1
#define IP_RADS_ON 0

#define OP_OB_CIRC_ON 0
#define OP_WB_CIRC_ON 1

#define UPDATES 60
#define INPUTS 0

#endif
