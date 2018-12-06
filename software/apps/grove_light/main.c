#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_twi_mngr.h"
#include "app_util_platform.h"
#include "nordic_common.h"
#include "app_timer.h"
#include "nrf_drv_clock.h"
#include "nrf_power.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_drv_gpiote.h"
#include "nrf.h"
#include "app_error.h"
#include "buckler.h"
#include "math.h"

#include "../../libraries/grove_light/tsl2561.h"

//Variables to maintain thresholds
bool update_thresh = false;
unsigned int upper;
unsigned int lower;

unsigned int global_count;

//Macro for defining a TWI instance
//Param 1: instance name; Param 2: queue size; Param 3: index
NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

//This function will be called when the tsl2561 is interrupted
//For now, read the lux, update the thresholds accordingly and clear the interrupt.
//TODO: Send out packets to the gateway
void interrupt_handler(){
	nrf_drv_gpiote_out_toggle(14);
	//printf("INTERRUPT HAPPENED!");
	//printf("Lux Code 0: %i\n", tsl2561_read_lux_code0());
	//printf("Lux Code 1: %i\n", tsl2561_read_lux_code1());
	unsigned int lux0_int = tsl2561_read_lux_code0();
	unsigned int lux1_int = tsl2561_read_lux_code1();
	nrf_delay_ms(1500);
	unsigned int lux0_reg = tsl2561_read_lux_code0();
	unsigned int lux1_reg = tsl2561_read_lux_code1();
	double ratio = ((double) abs(lux0_reg - lux0_int)) / ((double) abs(lux1_reg - lux1_int));
	
	upper = lux0_reg + 50;
	lower = (lux0_reg <= 5) ? 0 : lux0_reg - 50;

	tsl2561_write_threshold_upper(upper);
	tsl2561_write_threshold_lower(lower);
	
	tsl2561_clear_interrupt();
	
	if (ratio <= 2) {
		//printf("FALSE POSITIVE!\n");
		global_count++;
		printf("ACTUAL BLINK! Count: %i\n", global_count);
	//} else {
	}

	//printf("New Upper: %i, New Lower: %i\n", upper, lower);
	//printf("Ratio: %f\n", ratio);
}

//Want to tell the nRF to trigger an interrupt when 'pin' transitions from high to low
//The GPIO here should be whichever one the INT line of tsl2561 is wired to
static void configure_interrupt(int pin, void *callback){
	ret_code_t err_code;

	err_code = nrf_drv_gpiote_init();
	APP_ERROR_CHECK(err_code);
	
	nrf_drv_gpiote_out_config_t out_config= GPIOTE_CONFIG_OUT_SIMPLE(true);

	err_code = nrf_drv_gpiote_out_init(14, &out_config);
	APP_ERROR_CHECK(err_code);

	nrf_drv_gpiote_in_config_t int_gpio_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(0);
	int_gpio_config.pull = NRF_GPIO_PIN_PULLUP;

	err_code = nrf_drv_gpiote_in_init(24, &int_gpio_config, interrupt_handler);
	APP_ERROR_CHECK(err_code);

	nrf_drv_gpiote_in_event_enable(24, 1);
}

//This will initialize our twi struct
//For now this is from the sample. I don't know where I2C_SCL,etc. are defined yet
//SCL: Clock line for I2C
//SDA: Data line for I2C
void twi_init(void){
	ret_code_t err_code;	

	const nrf_drv_twi_config_t twi_config = {
		.scl		= BUCKLER_SENSORS_SCL,
		.sda		= BUCKLER_SENSORS_SDA,
		.frequency 	= NRF_TWI_FREQ_400K,
	};
	
	//Initialize the instance and error if it is not initialized properly:
	//Param 1 points at the instance we made with "NRF_TWI..." above
	//Param 2 points at our driver configuration right above
	err_code = nrf_twi_mngr_init(&twi_mngr_instance, &twi_config);
	APP_ERROR_CHECK(err_code);
}

int main(void){
	//This either enables or disables dcdc converter 
	nrf_power_dcdcen_set(1);

	//Call our init function to get our twi instance
	twi_init();

	//Not exactly sure why this is necessary yet
	nrf_delay_ms(500);

	tsl2561_init(&twi_mngr_instance);	
	tsl2561_power_on(1);

	const tsl2561_config_t config = {
		.gain =  1,
		.int_time = 2,
		.int_mode = 1, //set to test mode (revert to 1 for proper operation mode)
		.persist = 1,
	};

	tsl2561_config(config);
	
	tsl2561_write_threshold_upper(100);
	tsl2561_write_threshold_lower(0);
	
	printf("Threshold lower: %i, Threshold Upper: %i\n", tsl2561_read_threshold_lower(), tsl2561_read_threshold_upper());

	configure_interrupt(BUCKLER_BUTTON0, interrupt_handler);

	while(1){
		__WFI();
		//nrf_drv_gpiote_out_toggle(14);
		//printf("Lux Value: %i\n", tsl2561_read_lux());
		//printf("Lux Code 0: %i\n", tsl2561_read_lux_code0());
		//printf("Lux Code 1: %i\n", tsl2561_read_lux_code1());
		//printf("Threshold lower: %i, Threshold Upper: %i\n", tsl2561_read_threshold_lower(), tsl2561_read_threshold_upper());
		//tsl2561_generate_interrupt();
		//printf("interrupt low\n");
		//printf("Looping\n");
		//printf("interrupt high\n");
		//nrf_delay_ms(2000);
	}
}
