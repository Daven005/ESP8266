/*
 * sysCfg.c
 *
 *  Created on: 9 Nov 2016
 *      Author: User
 */

#include <c_types.h>
#include <osapi.h>
#include <user_interface.h>
#include "sysCfg.h"

SYSCFG sysCfg;

void ICACHE_FLASH_ATTR initSysCfg(void) {
	os_memset(&sysCfg, 0x00, sizeof sysCfg);
	sysCfg.cfg_holder = CFG_HOLDER;
	os_sprintf(sysCfg.sta_ssid, "%s", STA_SSID);
	os_sprintf(sysCfg.sta_pwd, "%s", STA_PASS);
	sysCfg.sta_type = STA_TYPE;
	os_sprintf(sysCfg.deviceID_prefix, DEVICE_PREFIX);
	os_sprintf(sysCfg.device_id, "%s%lx", sysCfg.deviceID_prefix, system_get_chip_id());
	os_sprintf(sysCfg.mqtt_host, "%s", MQTT_HOST);
	sysCfg.mqtt_port = MQTT_PORT;
	os_sprintf(sysCfg.mqtt_user, "%s", MQTT_USER);
	os_sprintf(sysCfg.mqtt_pass, "%s", MQTT_PASS);
	sysCfg.security = DEFAULT_SECURITY; /* default non ssl */
	sysCfg.mqtt_keepalive = MQTT_KEEPALIVE;
	int idx;
	for (idx = 0; idx < MAP_TEMP_SIZE; idx++) {
		sysCfg.mapping[idx] = idx;
	}
	sysCfg.settings[SETTING_DHW_SET_POINT] = DEFAULT_DHW_SET_POINT;
	sysCfg.settings[SETTING_UFH_SET_POINT] = DEFAULT_UFH_SET_POINT;
	sysCfg.settings[SETTING_RADS_SET_POINT] = DEFAULT_RADS_SET_POINT;
	sysCfg.settings[SETTING_SET_POINT_DIFFERENTIAL] = DEFAULT_SET_POINT_DIFFERENTIAL;
	sysCfg.settings[SETTING_WB_IS_ON_TEMP] = DEFAULT_WB_IS_ON_TEMP;
	sysCfg.settings[SETTING_DHW_USE_ALL_HEAT] = DEFAULT_DHW_USE_ALL_HEAT;
	sysCfg.settings[SETTING_OUTSIDE_TEMP_COMP] = DEFAULT_OUTSIDE_TEMP_COMP;
	sysCfg.settings[SETTING_BOOST_TIME] = DEFAULT_BOOST_TIME;
	sysCfg.settings[SETTING_BOOST_AMOUNT] = DEFAULT_BOOST_AMOUNT;
	sysCfg.settings[SETTING_EMERGENCY_DUMP_TEMP] = DEFAULT_EMERGENCY_DUMP_TEMP;
	sysCfg.settings[SETTING_OB_PUMP_DELAY] = DEFAULT_OB_PUMP_DELAY;
	sysCfg.settings[SETTING_DHW_ON_HOUR] = DEFAULT_DHW_ON_HOUR;
	sysCfg.settings[SETTING_DHW_OFF_HOUR] = DEFAULT_DHW_OFF_HOUR;
	sysCfg.settings[SETTING_OB_FAULTY] = DEFAULT_OB_FAULTY;
	sysCfg.settings[SETTING_INVERT_OPS] = DEFAULT_INVERT_OPS;
	sysCfg.updates = UPDATES;
	sysCfg.inputs = INPUTS;
	os_sprintf(sysCfg.deviceName, "Boiler Control");
	os_sprintf(sysCfg.deviceLocation, "Unknown");
}
