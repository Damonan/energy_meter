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

static uint8_t int_status_buf[2] = {TSL2561_INT_CTRL, 0};

//Buffer to write/enable interrupts 
static uint8_t int_enable_buf[2] = {TSL2561_INT_CTRL, 0};

//Buffer to be used for data input (from channel 0 or 1)
static uint8_t lux0_read_buf[2] = {0};
static uint8_t lux1_read_buf[2] = {0};

static uint8_t config_buf[2] = {TSL2561_CONFIG, 0};

//Buffer will contain one of the 4 possible thresholds (hi/hi --> lo/lo)
static uint8_t thresh_buf[2] = {TSL2561_THRESH_HI, 0};

//Buffer to write integration time to the Grove
static uint8_t time_buf[2] = {TSL2561_INT_TIME, 6};

//Data addresses of the grove sensor
static const uint8_t tsl2561_lux_low_low_addr = TSL2561_LUX_LO_LO;
static const uint8_t tsl2561_lux_low_high_addr = TSL2561_LUX_LO_HI;
static const uint8_t tsl2561_lux_high_low_addr = TSL2561_LUX_HI_LO;
static const uint8_t tsl2561_lux_high_high_addr = TSL2561_LUX_HI_LO;

static tsl2561_read_lux_callback* lux_read_callback;
static tsl2561_interrupt_callback* interrupt_callback;

static void lux_callback(ret_code_t result, void* p_context);

////////////////////////////////////////////////////////////////
//           INTERNAL FUNCTIONS FOR TWI TRANSFERS             //
////////////////////////////////////////////////////////////////
           
static nrf_twi_mngr_transfer_t const int_status_transfer[] = {
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, int_status_buf, 1, NRF_TWI_MNGR_NO_STOP),
  NRF_TWI_MNGR_READ(TSL2561_ADDR, int_status_buf+1, 1, 0)
};


//Write to enable interrupts
static nrf_twi_mngr_transfer_t const int_enable_transfer[] = {
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, int_enable_buf, 2, NRF_TWI_MNGR_NO_STOP),
};

//Two possible reads: Channel 0 or Channel 1
static nrf_twi_mngr_transfer_t const lux0_read_transfer[] = {
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &tsl2561_lux_low_low_addr, 1, NRF_TWI_MNGR_NO_STOP),
  NRF_TWI_MNGR_READ(TSL2561_ADDR, lux0_read_buf, 1, 0),
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &tsl2561_lux_low_high_addr, 1, NRF_TWI_MNGR_NO_STOP),
  NRF_TWI_MNGR_READ(TSL2561_ADDR, lux0_read_buf+1, 1, 0)
};

static nrf_twi_mngr_transfer_t const lux1_read_transfer[] = {
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &tsl2561_lux_high_low_addr, 1, NRF_TWI_MNGR_NO_STOP),
  NRF_TWI_MNGR_READ(TSL2561_ADDR, lux1_read_buf, 1, 0),
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, &tsl2561_lux1_low_high_addr, 1, NRF_TWI_MNGR_NO_STOP),
  NRF_TWI_MNGR_READ(TSL2561_ADDR, lux1_read_buf+1, 1, 0)
};


static nrf_twi_mngr_transfer_t const config_write_transfer[] = {
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, config_buf, 2, 0)
};


//Write to one of the threshold registers
static nrf_twi_mngr_transfer_t const threshold_write_transfer[] = {
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, thresh_buf, 2, 0),
};


//Write to the timing register to set the integration time
static nrf_twi_mngr_transfer_t const int_time_write_transfer[] = {
  NRF_TWI_MNGR_WRITE(TSL2561_ADDR, time_buf, 2, 0),
};

static nrf_twi_mngr_transaction_t const lux_read_transaction =
{
  .callback = lux_callback,
  .p_user_data = NULL,
  .p_transfers = lux_read_transfer,
  .number_of_transfers = sizeof(lux_read_transfer)/sizeof(lux_read_transfer[0]),
  .p_required_twi_cfg = NULL
};

////////////////////////////////////////////////////////////////
//           EXTERNAL FUNCTIONS FOR TO BE USED IN APPS        //
////////////////////////////////////////////////////////////////

static void interrupt_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
  int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, int_status_transfer, sizeof(int_status_transfer)/sizeof(int_status_transfer[0]), NULL);
  APP_ERROR_CHECK(error);
  //TODO (not sure if this correct for grove)
  if(int_status_buf[1] == 1) {
    interrupt_callback();
  }
}

//Check website for comments
static float calc_lux(unsigned int iGain, unsigned int tInt, unsigned int ch0, unsigned int ch1, int iType) {
	unsigned long chScale;
	unsigned long channel1;
	unsigned long channel0;
	switch(tInt)
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

static void lux_callback(ret_code_t result, void* p_context) {
  lux_read_callback(calc_lux());
}

void tsl2561_init(const nrf_twi_mngr_t* instance) {
  twi_mngr_instance = instance;
}

void tsl2561_set_interrupt_callback(tsl2561_interrupt_callback* callback) {
  interrupt_callback = callback;

  // setup gpiote interrupt
  if (!nrf_drv_gpiote_is_init()) {
    nrf_drv_gpiote_init();
  }
  nrf_drv_gpiote_in_config_t int_gpio_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(0);
  int error = nrf_drv_gpiote_in_init(BUCKLER_LIGHT_INTERRUPT, &int_gpio_config, interrupt_handler);
  APP_ERROR_CHECK(error);
}

void tsl2561_enable_interrupt(void) {
  int_enable_buf[1] = 1;
  int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, int_enable_transfer, sizeof(int_enable_transfer)/sizeof(int_enable_transfer[0]), NULL);
  APP_ERROR_CHECK(error);
  error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, int_time_write_transfer, sizeof(int_time_write_transfer)/sizeof(int_time_write_transfer[0]), NULL);
  APP_ERROR_CHECK(error);

  nrf_drv_gpiote_in_event_enable(BUCKLER_LIGHT_INTERRUPT, 1);
}

void tsl2561_disable_interrupt(void) {
  int_enable_buf[1] = 0;
  int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, int_enable_transfer, sizeof(int_enable_transfer)/sizeof(int_enable_transfer[0]), NULL);
  APP_ERROR_CHECK(error);

  nrf_drv_gpiote_in_event_enable(BUCKLER_LIGHT_INTERRUPT, 0);
}

//To configure our timing: 
//	Gain options: 0 -> 1x, 1 -> 16x
//	Int_time options: 00 -> 13.7, 01 -> 101, 10 -> 402 (ms) 
void tsl2561_timing_config(tsl2561_timing_config_t timing_config) {
  uint8_t timing_config_byte = timing_config.gain << 4 |
                               timing_config.int_time;

  timing_config_buf[1] = timing_config_byte;

  int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, timing_config_write_transfer, sizeof(timing_config_write_transfer)/sizeof(timing_config_write_transfer[0]), NULL);
  APP_ERROR_CHECK(error);
}

//To configure our interrupts:
//	Interrupt Mode options: 00 -> Disabled, 01 -> Level, 10 -> SMBAlert, 11 -> Test
//	Persist Options: 0 -> Every cycle, 1 -> Any 1 value, N -> N time periods (to 15)
void tsl2561_interrupt_config(tsl2561_interrupt_config_t interrupt_config) {
  uint8_t interrupt_config_byte = interrupt_config.int_mode << 4 |
                                  interrupt_config.persist;

  interrupt_config_buf[1] = interrupt_config_byte;

  int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, interrupt_config_write_transfer, sizeof(interrupt_config_write_transfer)/sizeof(interrupt_config_write_transfer[0]), NULL);
  APP_ERROR_CHECK(error);
}

void  tsl2561_set_read_lux_callback(tsl2561_read_lux_callback* callback) {
  lux_read_callback = callback;
}

void tsl2561_set_upper_threshold(float thresh) {
  uint8_t exp, mant = 0;
  ////printf("test #####");
  //calc_exp_mant(728, 0, &exp, &mant);
  //exp = 0;
  //mant = 0;
  //printf("upper #####\n");
  calc_exp_mant(thresh, 1, &exp, &mant);

  thresh_buf[0] = TSL2561_THRESH_HI;
  thresh_buf[1] = ((exp & 0x0F) << 4) | ((mant & 0xF0) >> 4);

  int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, threshold_write_transfer, sizeof(threshold_write_transfer)/sizeof(threshold_write_transfer[0]), NULL);
  APP_ERROR_CHECK(error);

}

void tsl2561_set_lower_threshold(float thresh) {
  uint8_t exp, mant = 0;
  //printf("lower #####\n");
  calc_exp_mant(thresh, 0, &exp, &mant);


  thresh_buf[0] = TSL2561_THRESH_LO;
  thresh_buf[1] = (exp << 4) | ((mant & 0xF0) >> 4);

  int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, threshold_write_transfer, sizeof(threshold_write_transfer)/sizeof(threshold_write_transfer[0]), NULL);
  APP_ERROR_CHECK(error);
}

void tsl2561_schedule_read_lux(void) {
  APP_ERROR_CHECK(nrf_twi_mngr_schedule(twi_mngr_instance, &lux_read_transaction));
}

float tsl2561_read_lux(void) {
  nrf_twi_mngr_perform(twi_mngr_instance, NULL, lux_read_transfer, sizeof(lux_read_transfer)/sizeof(lux_read_transfer[0]), NULL);
  return calc_lux();
}
