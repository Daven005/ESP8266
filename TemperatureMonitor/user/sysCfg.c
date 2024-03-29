/*
/* config.c
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <c_types.h>
#include <osapi.h>
#include <spi_flash.h>
#include <user_interface.h>
//#include "espmissingincludes.h"
#include "sysCfg.h"
#include "debug.h"

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

	sysCfg.security = DEFAULT_SECURITY;	/* default non ssl */
	sysCfg.mqtt_keepalive = MQTT_KEEPALIVE;

	int idx;
	for (idx=0; idx < MAP_TEMP_SIZE; idx++) {
		sysCfg.mapping[idx] = idx;
	}

	sysCfg.updates = UPDATES;
#ifdef INPUTS
	sysCfg.inputs = INPUTS;
#else
	sysCfg.inputs = 0;
#endif
#ifdef OUTPUTS
	sysCfg.outputs = OUTPUTS;
#else
	sysCfg.outputs = 0;
#endif
	os_sprintf(sysCfg.deviceName, "Temperature Monitor");
	os_sprintf(sysCfg.deviceLocation, "Unknown");
	}
