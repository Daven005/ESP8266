/*
 * pdf8574.c
 * IO driver for PDF8574 IO expander
 *  Created on: 15 Oct 2018
 *      Author: User
 */


#include <c_types.h>
#include <osapi.h>
#include <i2c_master.h>
#include "debug.h"
#include "pdf8574.h"

void ICACHE_FLASH_ATTR initExpander(void) {
	i2c_master_gpio_init();
	i2c_master_init();

//	i2c_master_start();
//
//	i2c_master_writeByte((uint8)(PDF8574_ADDR << 1));
//	if (!i2c_master_checkAck()) {
//		TESTP("i2c error (no ACK for address)\n");
//		i2c_master_stop();
//		return;
//	}
//	i2c_master_writeByte(0xff); // Same as initial state
//	if (!i2c_master_checkAck()) {
//		TESTP("i2c error (No ACK for send)\n");
//	}
//	i2c_master_stop();
}

void ICACHE_FLASH_ATTR setExpanderOutput(uint8 op) {
	i2c_master_start();
	i2c_master_writeByte((uint8)(PDF8574_ADDR << 1));
	if (!i2c_master_checkAck()) {
		TESTP("i2c error (no ACK for address)\n");
		i2c_master_stop();
		return;
	}
	i2c_master_writeByte(~op);
	if (!i2c_master_checkAck()) {
		TESTP("i2c error (No ACK for send)\n");
	}
	i2c_master_stop();
}

uint8 ICACHE_FLASH_ATTR getExpanderOutput(void) {
	uint8 data;

	i2c_master_start();

	i2c_master_writeByte((uint8)((PDF8574_ADDR << 1) | 1));
	if (!i2c_master_checkAck()) {
		TESTP("i2c error (No ACK for recv Address)\n");
		i2c_master_stop();
		return 0;
	}
	data = i2c_master_readByte();
	i2c_master_send_nack();
	i2c_master_stop();
	return ~data;
}

