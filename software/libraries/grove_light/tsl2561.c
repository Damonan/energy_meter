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

static const nrf_twi_mngr_t* twi_mngr_instance;

//static uint8_t int_status_buf[2] = {TSL2561_INT_CTRL, 0};

//Buffer to write/enable interrupts 
//static uint8_t int_enable_buf[2] = {TSL2561_INT_CTRL, 0};

//Buffer to be used for data input (from channel 0 or 1)
//static uint8_t lux0_read_buf[2] = {0};
//static uint8_t lux1_read_buf[2] = {0};

//static uint8_t config_buf[2] = {TSL2561_CONFIG, 0};

//Buffer will contain one of the 4 possible thresholds (hi/hi --> lo/lo)
//static uint8_t thresh_buf[2] = {TSL2561_THRESH_HI, 0};

//Buffer to write integration time to the Grove
//static uint8_t time_buf[2] = {TSL2561_INT_TIME, 6};

//Data addresses of the grove sensor
static const uint8_t ch0_low_addr = TSL2561_LUX0_LO;
static const uint8_t ch0_high_addr = TSL2561_LUX0_HI;
static const uint8_t ch1_low_addr = TSL2561_LUX1_LO;
static const uint8_t ch1_high_addr = TSL2561_LUX1_HI;

//Test read from ID register
static uint8_t ID_test_buf[2] = {TSL2561_ID, 0};

static uint8_t power_buf[2] = {TSL2561_POWER, 0x3};

static uint8_t config_timing_buf[2] = {TSL2561_TIMING, 0};

static uint8_t config_interrupt_buf[2] = {TSL2561_INT, 0};

static uint8_t read_lux_buf[2] = {0};

static uint8_t read_lux_test[2] = {TSL2561_LUX0_LO, 0};

unsigned int iGain;
unsigned int tInt;
int iType;

//static tsl2561_read_lux_callback* lux_read_callback;
//static tsl2561_interrupt_callback* interrupt_callback;

//static void lux_callback(ret_code_t result, void* p_context);

////////////////////////////////////////////////////////////////
//           INTERNAL FUNCTIONS FOR TWI TRANSFERS             //
////////////////////////////////////////////////////////////////
           
static nrf_twi_mngr_transfer_t const ID_transfer[] = {
  	NRF_TWI_MNGR_WRITE(TSL2561_ADDR, ID_test_buf, 1, NRF_TWI_MNGR_NO_STOP),
	NRF_TWI_MNGR_READ(TSL2561_ADDR, ID_test_buf+1, 1, 0)
};

static nrf_twi_mngr_transfer_t const power_control[] = {
  	NRF_TWI_MNGR_WRITE(TSL2561_ADDR, power_buf, 2, 0)
};

static nrf_twi_mngr_transfer_t const config_timing[]={
  	NRF_TWI_MNGR_WRITE(TSL2561_ADDR, config_timing_buf, 2, 0)
};

static nrf_twi_mngr_transfer_t const config_interrupt[]={
	NRF_TWI_MNGR_WRITE(TSL2561_ADDR, config_interrupt_buf, 2, 0)
};

static nrf_twi_mngr_transfer_t const write_thresh_low[]={
	
};

static nrf_twi_mngr_transfer_t const write_thresh_high[]={

};

static nrf_twi_mngr_transfer_t const read_lux_ch0[]={
 	NRF_TWI_MNGR_WRITE(TSL2561_ADDR, read_lux_test, 1, NRF_TWI_MNGR_NO_STOP),
	NRF_TWI_MNGR_READ(TSL2561_ADDR, read_lux_test+1, 1, 0), 
	//NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &ch0_high_addr, 1, NRF_TWI_MNGR_NO_STOP),
	//NRF_TWI_MNGR_READ(TSL2561_ADDR, read_lux_buf+1, 1, 0)
};

static nrf_twi_mngr_transfer_t const read_lux_ch1[]={
 	NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &ch1_low_addr, 1, NRF_TWI_MNGR_NO_STOP),
	NRF_TWI_MNGR_READ(TSL2561_ADDR, read_lux_buf, 1, 0), 
	NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &ch1_high_addr, 1, NRF_TWI_MNGR_NO_STOP),
	NRF_TWI_MNGR_READ(TSL2561_ADDR, read_lux_buf+1, 1, 0)
};

/*
float calc_lux(unsigned int channel_in){
	//TODO
}
*/

////////////////////////////////////////////////////////////////
//           EXTERNAL FUNCTIONS TO BE USED IN APPS            //
////////////////////////////////////////////////////////////////

void tsl2561_init(const nrf_twi_mngr_t* instance) {
  	twi_mngr_instance = instance;
}

void tsl2561_ID_transfer(void){
 	int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, ID_transfer, sizeof(ID_transfer)/sizeof(ID_transfer[0]), NULL);
  	APP_ERROR_CHECK(error);
}

void tsl2561_power_on(bool on){
	power_buf[1] = on ? 3 : 0;
	int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, power_control, sizeof(power_control)/sizeof(power_control[0]), NULL);
	APP_ERROR_CHECK(error);
}

void tsl2561_config(tsl2561_config_t config){
	config_timing_buf[1] = (config.gain << 4) | (config.int_time & 1);
	config_interrupt_buf[1] = (config.int_mode << 4) | (config.persist);

	int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, config_timing, sizeof(config_timing)/sizeof(config_timing[0]), NULL);
	APP_ERROR_CHECK(error);

	error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, config_interrupt, sizeof(config_interrupt)/sizeof(config_interrupt[0]), NULL);
	APP_ERROR_CHECK(error);
}

void  tsl2561_read_lux(bool channel){
	//int error;
	//if (channel){
	  //     	error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, read_lux_ch1, sizeof(read_lux_ch1)/sizeof(read_lux_ch1[0]), NULL);
	//} else {
	       int  error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, read_lux_ch0, sizeof(read_lux_ch0)/sizeof(read_lux_ch0[0]), NULL);
	//}
	APP_ERROR_CHECK(error);
	
	printf("lux_read_buf[0]: %x, lux_read_buf[1]: %x\n", read_lux_buf[0], read_lux_buf[1]);
}

