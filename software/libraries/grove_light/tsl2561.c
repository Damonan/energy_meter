#include <math.h>

#include "nrf_drv_gpiote.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#include "tsl2561.h"
#include "buckler.h"

//CONSTANTS TO CALCULATE LUX (see website)
#define LUX_SCALE	14
#define RATIO_SCALE	9
#define CH_SCALE	10
#define CHSCALE_TINT0	0x7517
#define CHSCALE_TINT1	0x0fe7

//TWI instance to be used throughout
static const nrf_twi_mngr_t* twi_mngr_instance;

//Data addresses of the grove sensor
static const uint8_t ch0_low_addr = TSL2561_LUX0_LO;
static const uint8_t ch0_high_addr = TSL2561_LUX0_HI;
static const uint8_t ch1_low_addr = TSL2561_LUX1_LO;
static const uint8_t ch1_high_addr = TSL2561_LUX1_HI;

//Threshold addresses of the grove sensor
static const uint8_t thresh0_low_addr = TSL2561_THRESH0_LO;
static const uint8_t thresh0_high_addr = TSL2561_THRESH0_HI;
static const uint8_t thresh1_low_addr = TSL2561_THRESH1_LO;
static const uint8_t thresh1_high_addr = TSL2561_THRESH1_HI;

//Various buffer for reading/write bytes to the corresponding register
static uint8_t ID_test_buf[1] = {0};
static uint8_t power_buf[1] = {0x3};
static uint8_t config_timing_buf[1] = {0};
static uint8_t config_interrupt_buf[1] = {0};

//Read data from our lux into these buffers
static uint8_t read_lux0_buf[2] = {0};
static uint8_t read_lux1_buf[2] = {0};

//Read/write the threshold values
static uint8_t read_thresh_buf[2] = {0};
static uint8_t write_thresh_buf[2] = {0};

//Local config settings to be used in calc_lux
unsigned int iGain;
unsigned int tInt;

//static tsl2561_read_lux_callback* lux_read_callback;
//static tsl2561_interrupt_callback* interrupt_callback;

//static void lux_callback(ret_code_t result, void* p_context);

////////////////////////////////////////////////////////////////
//           INTERNAL FUNCTIONS FOR TWI TRANSFERS             //
////////////////////////////////////////////////////////////////

void read_data (uint8_t addr, uint8_t length,  uint8_t buf[]){
	uint8_t command;
	if (length == 2){
		command = (0xA0) | addr;
	} else {
		command = (0x80) | addr;
	}
	nrf_twi_mngr_transfer_t transaction[] = {
		NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &command, 1, NRF_TWI_MNGR_NO_STOP),
		NRF_TWI_MNGR_READ(TSL2561_ADDR, buf, length, 0)
	};

	int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, transaction, sizeof(transaction)/sizeof(transaction[0]), NULL); 
	APP_ERROR_CHECK(error);
}

void write_data (uint8_t addr, uint8_t length, uint8_t buf[]){
	uint8_t command;
	if (length == 2){
		command = (0xA0) | addr;
	} else {
		command = (0x80) | addr;
	}
	nrf_twi_mngr_transfer_t transaction[] = {
		NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &command, 1, NRF_TWI_MNGR_NO_STOP),
		NRF_TWI_MNGR_WRITE(TSL2561_ADDR, buf, length, 0)
	};

	int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, transaction, sizeof(transaction)/sizeof(transaction[0]), NULL); 
	APP_ERROR_CHECK(error);
}



unsigned long calc_lux(void){
	//Get values from our read buffers
	unsigned int ch0 = (read_lux0_buf[1] << 8) | read_lux0_buf[0]; 
	unsigned int ch1 = (read_lux1_buf[1] << 8) | read_lux1_buf[0];
	
	//See tsl2561 datasheet page 26 for comments
	unsigned long chScale;
	unsigned long channel1;
	unsigned long channel0;
	switch (tInt)
	{
		case 0:
			chScale = CHSCALE_TINT0;
			break;
		case 1:
			chScale = CHSCALE_TINT1;
			break;
		default:
			chScale = (1 << CH_SCALE);
			break;
	}

	if (!iGain) chScale = chScale << 4;

	channel0 = (ch0 * chScale) >> CH_SCALE;
	channel1 = (ch1 * chScale) >> CH_SCALE;

	unsigned long ratio1 = 0;
	if (channel0 != 0) ratio1 = (channel1 << (RATIO_SCALE+1)) / channel0;

	unsigned long ratio = (ratio1 + 1) >> 1;

	unsigned int b, m;
	if ((ratio >= 0) && (ratio <= K1T))
		{b=B1T; m=M1T;}
	else if (ratio <= K2T)
		{b=B2T; m=M2T;}
	else if (ratio <= K3T)
		{b=B3T; m=M3T;}
	else if (ratio <= K4T)
		{b=B4T; m=M4T;}
	else if (ratio <= K5T)
		{b=B5T; m=M5T;}
	else if (ratio <= K6T)
		{b=B6T; m=M6T;}
	else if (ratio <= K7T)
		{b=B7T; m=M7T;}
	else if (ratio <= K8T)
		{b=B8T; m=M8T;}

	unsigned long temp;
	temp = ((channel0 * b) - (channel1 * m));
	
	if (temp < 0) temp = 0;

	temp += (1 << (LUX_SCALE-1));
	
	unsigned long lux = temp >> LUX_SCALE;
	
	return(lux);
}


////////////////////////////////////////////////////////////////
//           EXTERNAL FUNCTIONS TO BE USED IN APPS            //
////////////////////////////////////////////////////////////////

void tsl2561_init(const nrf_twi_mngr_t* instance) {
  	twi_mngr_instance = instance;
}

//Used for testing purposes
void tsl2561_ID_transfer(void){
	read_data(TSL2561_ID, 1, ID_test_buf);
	printf("ID_test_buf[0]: %x\n", ID_test_buf[0]);
}

//To control the power of the device
void tsl2561_power_on(bool on){
	power_buf[0] = on ? 3 : 0;
	write_data(TSL2561_POWER, 1, power_buf);
}

//To configure the timing and interrupt options of our device
void tsl2561_config(tsl2561_config_t config){
	config_timing_buf[0] = (config.gain << 4) | (config.int_time & 11);
	config_interrupt_buf[0] = (config.int_mode << 4) | (config.persist);
	
	tInt = config.int_time;
	iGain = config.gain;

	write_data(TSL2561_TIMING, 1, config_timing_buf);
	write_data(TSL2561_INT, 1, config_interrupt_buf);
}

void tsl2561_clear_interrupt(){
	uint8_t command = TSL2561_CLEAR_INT;
	nrf_twi_mngr_transfer_t transaction[] = {
		NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &command, 1, NRF_TWI_MNGR_NO_STOP),
	};

	int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, transaction, sizeof(transaction)/sizeof(transaction[0]), NULL); 
	APP_ERROR_CHECK(error);
}

void tsl2561_write_threshold(float threshold){
	//TODO: how to convert these values?
	//write_thresh_buf[0] = 
	//write_thresh_buf[1] = 

	//just testing for now
	write_thresh_buf[0] = 10;
	write_thresh_buf[1] = 0x23;
	write_data(TSL2561_THRESH0_LO, 2, write_thresh_buf);
}

uint8_t tsl2561_read_threshold(void){
	read_data(TSL2561_THRESH0_LO, 2, read_thresh_buf);
	return (read_thresh_buf[0]);
}

unsigned long tsl2561_read_lux(void){
	read_data(TSL2561_LUX0_LO, 2, read_lux0_buf);
	read_data(TSL2561_LUX1_LO, 2, read_lux1_buf);	
	//printf("lux_read0_buf[0]: %x, lux_read0_buf[1]: %x\n", read_lux0_buf[0], read_lux0_buf[1]);
	//printf("lux_read1_buf[0]: %x, lux_read1_buf[1]: %x\n", read_lux1_buf[0], read_lux1_buf[1]);
	return(calc_lux());
}

