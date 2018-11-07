// Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX44009.pdf

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "nrf_twi_mngr.h"

#define TSL2561_ADDR       	0x29
#define TSL2561_CLEAR_INT	0xc0
#define TSL2561_POWER		0x00
#define TSL2561_TIMING	   	0x01
#define TSL2561_THRESH_LO_LO  	0x02
#define TSL2561_THRESH_LO_HI 	0x03
#define TSL2561_THRESH_HI_LO  	0x04
#define TSL2561_THRESH_HI_HI 	0x05
#define TSL2561_INT	 	0x06
#define TSL2561_ID		0x0a
#define TSL2561_LUX0_LO     	0x0c
#define TSL2561_LUX0_HI     	0x0d
#define TSL2561_LUX1_LO	    	0x0e
#define TSL2561_LUX1_HI     	0x0f

//Grove sensor coefficients (Using TMB Package coeffs, not sure which one we have)
//These are used in lux calculation
#define K1T	0x0040
#define B1T	0x01f2
#define M1T	0x01be

#define K2T	0x0080
#define B2T	0x0214
#define M2T	0x02d1

#define K3T	0x00c0
#define B3T	0x023f
#define M3T	0x037b

#define K4T	0x0100
#define B4T	0x0270
#define M4T	0x03fe

#define K5T	0x0138
#define B5T	0x016f
#define M5T	0x01fc

#define K6T	0x019a
#define B6T	0x00d2
#define M6T	0x00fb

#define K7T	0x029a
#define B7T	0x0018
#define M7T	0x0012

#define K8T	0x029a
#define B8T	0x0000
#define M8T	0x0000

typedef void tsl2561_read_lux_callback(float lux);
typedef void tsl2561_interrupt_callback(void);

typedef struct {
  bool gain;
  uint8_t int_time;
  uint8_t int_mode; //interrupt mode
  uint8_t persist;	  //persist option
} tsl2561_config_t;

void  tsl2561_init(const nrf_twi_mngr_t* instance);
void  tsl2561_ID_transfer(void);
void  tsl2561_power_on(bool on);
void  tsl2561_config(tsl2561_config_t config);
unsigned int tsl2561_read_lux(void);
void tsl2561_generate_interrupt(void);
void tsl2561_clear_interrupt(void);
