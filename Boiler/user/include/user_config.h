#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A5	/* Change this value to load default configurations */
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

#define TEST_OP 2
#define SWITCH 0 // GPI00
#define LED 5 // NB same as an_t t);

#define DS18B20_MUX		PERIPHS_IO_MUX_GPIO2_U
#define DS18B20_FUNC	FUNC_GPIO2
#define DS18B20_PIN		2

#define IP4 16

/*DEFAULT CONFIGURATIONS*/

#define MQTT_HOST			"192.168.1.100" //"broker.mqttdashboard.com"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		512
#define MQTT_KEEPALIVE		120	 /*second*/

#define DEVICE_PREFIX		"Hollies"
#define MQTT_USER			""
#define MQTT_PASS			""

#define STA_SSID "EE-berry"
#define STA_PASS "fog-phone-deep"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/

#define DEFAULT_SECURITY	0
#define QUEUE_BUFFER_SIZE	4096

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#define NAME_SIZE 32
#define MAP_TEMP_SIZE 15
#define SETTINGS_SIZE 20
#define MAX_OUTPUT 4
#define MAX_INPUT 5
#define REQUIRED_SENSORS 6

#define MAX_TEMPERATURE_SENSOR 15
#define INPUT_SENSOR_ID_START 10
#define OUTPUT_SENSOR_ID_START 20

#define SETTING_DHW_SET_POINT 0
#define SETTING_UFH_SET_POINT 1
#define SETTING_RADS_SET_POINT 2
#define SETTING_SET_POINT_DIFFERENTIAL 3
#define SETTING_WB_IS_ON_TEMP 4
#define SETTING_DHW_USE_ALL_HEAT 5
#define SETTING_OUTSIDE_TEMP_COMP 6
#define SETTING_BOOST_TIME 7
#define SETTING_BOOST_AMOUNT 8
#define SETTING_EMERGENCY_DUMP_TEMP 9
#define SETTING_OB_PUMP_DELAY 10
#define SETTING_DHW_ON_HOUR 11
#define SETTING_DHW_OFF_HOUR 12

#define DEFAULT_DHW_SET_POINT 60
#define DEFAULT_UFH_SET_POINT 50
#define DEFAULT_RADS_SET_POINT 66
#define DEFAULT_SET_POINT_DIFFERENTIAL 4
#define DEFAULT_WB_IS_ON_TEMP 60
#define DEFAULT_DHW_USE_ALL_HEAT 50
#define DEFAULT_OUTSIDE_TEMP_COMP 10
#define DEFAULT_BOOST_TIME 5
#define DEFAULT_BOOST_AMOUNT 2
#define DEFAULT_EMERGENCY_DUMP_TEMP 88
#define DEFAULT_OB_PUMP_DELAY 0
#define DEFAULT_DHW_ON_HOUR 7
#define DEFAULT_DHW_OFF_HOUR 22

#define SET_MINIMUM 0
#define SET_MAXIMUM 95

#define MAP_TEMP_TS_TOP 0
#define MAP_TEMP_TS_MIDDLE 1
#define MAP_TEMP_TS_BOTTOM 2
#define MAP_WB_FLOW_TEMP 3
#define MAP_OB_FLOW_TEMP 4
#define MAP_HEATING_RETURN_TEMP 5
#define MAP_HEATING_FLOW_TEMP 6
#define MAP_TEMP_TS_CYLINDER 7 // Physically higher than MIDDLE
#define MAP_SOLAR_TEMP 9

// Derived temperatures
#define MAP_CURRENT_CH_SET_POINT 10
#define MAP_CURRENT_DHW_SET_POINT 11
#define MAP_OUTSIDE_TEMP 12

#define IP_UFH_ON 0
#define IP_RADS_ON 1
#define IP_WB_SUPPLY_ON 2
#define IP_FAMILY_UFH_ON 3
#define IP_LOUNGE_UFH_ON 4

#define OP_WB_CIRC_ON 0
#define OP_OB_CIRC_ON 1
#define OP_OB_ON 2
#define OP_EMERGENCY_DUMP_ON 3

#define UPDATES 60

#endif
