#ifndef __DS18B20_H__
#define __DS18B20_H__

#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"

// Pin definitions in user_config.h

#define DS1820_WRITE_SCRATCHPAD 	0x4E
#define DS1820_READ_SCRATCHPAD      0xBE
#define DS1820_COPY_SCRATCHPAD 		0x48
#define DS1820_READ_EEPROM 			0xB8
#define DS1820_READ_PWRSUPPLY 		0xB4
#define DS1820_SEARCHROM 			0xF0
#define DS1820_SKIP_ROM             0xCC
#define DS1820_READROM 				0x33
#define DS1820_MATCHROM 			0x55
#define DS1820_ALARMSEARCH 			0xEC
#define DS1820_CONVERT_T            0x44
// DS18x20 family codes
#define DS18S20		0x10
#define DS18B20 	0x28


static uint16_t oddparity[16] =
		{ 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 };

void ds_init(void);
int ds_search(uint8_t *addr);
void select(const uint8_t rom[8]);
void ds1q8B20_skip(void);
void reset_search(void);
uint8_t reset(void);
void write(uint8_t v, int power);
void write_bit(int v);
uint8_t read(void);
int read_bit(void);
uint8_t crc8(const uint8_t *addr, uint8_t len);
uint16_t crc16(const uint16_t *data, const uint16_t len);

#endif
