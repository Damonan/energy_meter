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

#include "../../libraries/grove_light/tsl2561.h"

//Variables to maintain thresholds
bool update_thresh = false;
unsigned int upper;
unsigned int lower;

//Macro for defining a TWI instance
//Param 1: instance name; Param 2: queue size; Param 3: index
NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

//This function will be called when the tsl2561 is interrupted
//For now, read the lux, update the thresholds accordingly and clear the interrupt.
//TODO: Send out packets to the gateway
static void interrupt_handler(){
	unsigned int lux = tsl2561_read_lux();
	upper = lux + lux*0.10;
	lower = lux - lux*0.10;

	tsl2561_write_threshold_upper(upper);
	tsl2561_write_threshold_lower(lower);
	
	tsl2561_clear_interrupt();
	
	printf("Interrupt happened");
}

/* Some thoughts on how to get an interrupt working
void GPIOTE_INTERRUPT_CONFIG(int pin){
	//Set pin as HiToLo event trigger
	NRF_GPIOTE->CONFIG[0] = (2 << 16) | (pin << 8) | 1;
	NRF_GPIOTE->INTENSET = 1;
}

void GPIO_LED_CONFIG(void){
	NRF_GPIO->PIN_CNF[0] = 1;
}

void GPIOTE_IRQHandler(void){
	NRF_GPIOTE->EVENTS_IN[0] = 0;
	NRF_GPIOTE->TASKS_SET[0] = 1;
	printf("Interrupt happened\n");
}
*/

//Want to tell the nRF to trigger an interrupt when 'pin' transitions from high to low
//The GPIO here should be whichever one the INT line of tsl2561 is wired to
static void configure_interrupt(int pin, void *callback){
	if (!nrf_drv_gpiote_is_init()){
		nrf_drv_gpiote_init();
	}
	nrf_drv_gpiote_in_config_t int_gpio_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(0);
	int error = nrf_drv_gpiote_in_init(pin, &int_gpio_config, callback);
	APP_ERROR_CHECK(error);
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
		.gain =  0,
		.int_time = 2,
		.int_mode = 11, //set to test mode (revert to 1 for proper operation mode)
		.persist = 1,
	};

	tsl2561_config(config);
	
	//configure_interrupt(13, interrupt_handler);

	/*From interrupts example
	ret_code_t error_code = NRF_SUCCESS;

	error_code = NRF_LOG_INIT(NULL);
	APP_ERROR_CHECK(error_code);
	NRF_LOG_DEFAULT_BACKENDS_INIT();
	printf("Log initialized!\n");

	GPIOTE_INTERRUPT_CONFIG(23);
	GPIO_LED_CONFIG();
	*/

	tsl2561_write_threshold_upper(10000);
	tsl2561_write_threshold_lower(0);

	printf("Threshold lower: %i, Threshold Upper: %i\n", tsl2561_read_threshold_lower(), tsl2561_read_threshold_upper());

	while(1){
		//printf("Lux Value: %i\n", tsl2561_read_lux());
		//tsl2561_generate_interrupt();
		//printf("interrupt low\n");
		printf("Looping\n");
		nrf_delay_ms(2500);
		tsl2561_clear_interrupt();
		//printf("interrupt high\n");
		//nrf_delay_ms(2500);
	}
}
