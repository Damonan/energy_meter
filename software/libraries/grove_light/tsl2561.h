// Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX44009.pdf

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "nrf_twi_mngr.h"

#define TSL2561_ADDR       	0x29
#define TSL2561_INT_TIME   	0x01
#define TSL2561_THRESH_LO_LO  	0x02
#define TSL2561_THRESH_LO_HI 	0x03
#define TSL2561_THRESH_HI_LO  	0x04
#define TSL2561_THRESH_HI_HI 	0x05
#define TSL2561_INT_CTRL 	0x06
#define TSL2561_LUX0_LO     	0x0c
#define TSL2561_LUX0_HI     	0x0d
#define TSL2561_LUX1_LO	    	0x0e
#define TSL2561_LUX1_HI     	0x0f

//Grove sensor coefficients (Using TMB Package coeffs, not sure which one we have)
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
  bool continuous;  // enable continuous sample mode
  bool manual;      // allow manual IC configuration
  bool cdr;         // perform current division for high brightness
  uint8_t int_time; // integration timing, (automatically set if manual = 0)
} tsl2561_config_t;

void  tsl2561_init(const nrf_twi_mngr_t* instance);
void  tsl2561_set_interrupt_callback(tsl2561_interrupt_callback* callback);
void  tsl2561_enable_interrupt(void);
void  tsl2561_disable_interrupt(void);
void  tsl2561_config(tsl2561_config_t config);
void  tsl2561_set_read_lux_callback(tsl2561_read_lux_callback* callback);
void  tsl2561_set_upper_threshold(float thresh);
void  tsl2561_set_lower_threshold(float thresh);
void  tsl2561_schedule_read_lux(void);
float tsl2561_read_lux(void);
