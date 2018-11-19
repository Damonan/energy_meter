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
static const uint8_t thresh0_low_addr = TSL2561_THRESH_LO_LO;
static const uint8_t thresh0_high_addr = TSL2561_THRESH_LO_HI;
static const uint8_t thresh1_low_addr = TSL2561_THRESH_HI_LO;
static const uint8_t thresh1_high_addr = TSL2561_THRESH_HI_HI;

//READ BUFFERS
static uint8_t ID_test_buf[1] = {0};
static uint8_t read_lux0_buf[2] = {0};
static uint8_t read_lux1_buf[2] = {0};
static uint8_t read_thresh_buf[2] = {0};

//WRITE BUFFERS
static uint8_t power_buf[2] = {0x3};
static uint8_t config_timing_buf[2] = {0};
static uint8_t config_interrupt_buf[2] = {0};
static uint8_t write_thresh_buf[3] = {0};
static uint8_t write_test_int[2] = {0};

//Local config settings to be used in calc_lux
unsigned int iGain;
unsigned int tInt;

//static void lux_callback(ret_code_t result, void* p_context);

////////////////////////////////////////////////////////////////
//           INTERNAL FUNCTIONS FOR TWI TRANSFERS             //
////////////////////////////////////////////////////////////////

//General I2C read data protocol to be used with the buffers above
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

//General I2C write data protocl to be used with the buffers above
void write_data (uint8_t addr, uint8_t length, uint8_t buf[]){
	if (length == 2){
		buf[0] = (0xA0) | addr;
	} else {
		buf[0] = (0x80) | addr;
	}

	nrf_twi_mngr_transfer_t transaction[] = {
		NRF_TWI_MNGR_WRITE(TSL2561_ADDR, buf, length+1, 0)
	};

	int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, transaction, sizeof(transaction)/sizeof(transaction[0]), NULL); 
	APP_ERROR_CHECK(error);
}

//Lux calculation taken directly from the datasheet
unsigned int calc_lux(void){
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

//Initialize our twi instance to be used throughout operation
void tsl2561_init(const nrf_twi_mngr_t* instance) {
  	twi_mngr_instance = instance;
}

//Used for testing purposes. The ID register should always return the same value
void tsl2561_ID_transfer(void){
	read_data(TSL2561_ID, 1, ID_test_buf);
	printf("ID_test_buf[0]: %x\n", ID_test_buf[0]);
}

//To control the power of the device, write a byte to the control register
void tsl2561_power_on(bool on){
	power_buf[1] = on ? 3 : 0;
	write_data(TSL2561_POWER, 1, power_buf);
}

//To configure the timing and interrupt options of our device
void tsl2561_config(tsl2561_config_t config){
	config_timing_buf[1] = (config.gain << 4) | (config.int_time & 0x3);
	config_interrupt_buf[1] = (config.int_mode << 4) | (config.persist);
	
	tInt = config.int_time;
	iGain = config.gain;

	printf("config_timing_buf[0]: %x, config_interrupt_buf[0]: %x\n", config_timing_buf[0], config_interrupt_buf[0]);

	write_data(TSL2561_TIMING, 1, config_timing_buf);
	write_data(TSL2561_INT, 1, config_interrupt_buf);
}

//To clear interrupts, the tsl2561 requires a write to the command register
void tsl2561_clear_interrupt(void){
	uint8_t command = 0xc0;
	nrf_twi_mngr_transfer_t transaction[] = {
		NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &command, 1, 0),
	};

	int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, transaction, sizeof(transaction)/sizeof(transaction[0]), NULL); 
	APP_ERROR_CHECK(error);
}

//Write to the 16-bit thresholds by writing two bytes to the LO register without a stop condition in between
void tsl2561_write_threshold_lower(unsigned int threshold){
	write_thresh_buf[1] = threshold & 0xFF;
	write_thresh_buf[2] = (threshold >> 8) & 0xFF;
	write_data(TSL2561_THRESH_LO_LO, 2, write_thresh_buf);
}

void tsl2561_write_threshold_upper(unsigned int threshold){
	write_thresh_buf[1] = threshold & 0xFF;
	write_thresh_buf[2] = (threshold >> 8) & 0xFF;
	write_data(TSL2561_THRESH_HI_LO, 2, write_thresh_buf);
}

//Read to the 16-bit thresholds by reading two bytes from the LO register without a stop condition in between
unsigned int tsl2561_read_threshold_lower(void){
	read_data(TSL2561_THRESH_LO_LO, 2, read_thresh_buf);
	unsigned int threshold = (read_thresh_buf[1] << 8) | read_thresh_buf[0]; 
	return (threshold);
}

unsigned int tsl2561_read_threshold_upper(void){
	read_data(TSL2561_THRESH_HI_LO, 2, read_thresh_buf);
	unsigned int threshold = (read_thresh_buf[1] << 8) | read_thresh_buf[0];
	//printf("read_thresh_buf[0]: %x, read_thresh_buf[1]: %x\n", read_thresh_buf[0], read_thresh_buf[1]);
	return (threshold);
}

//Read the lux valus and return the calculated value from both
unsigned int tsl2561_read_lux(void){
	read_data(TSL2561_LUX0_LO, 2, read_lux0_buf);
	read_data(TSL2561_LUX1_LO, 2, read_lux1_buf);	
	return(calc_lux());
}

//Read lux raw values (just channel 0 for now)
uint16_t tsl2561_read_lux_code(void){
	read_data(TSL2561_LUX0_LO, 2, read_lux0_buf);
	return((read_lux0_buf[1] << 8) | (read_lux0_buf[0]));
}

//Generate a test interrupt
void tsl2561_generate_interrupt(void){
	write_test_int[0] = 0x30;
	write_data(TSL2561_INT , 1, write_test_int); 
}

