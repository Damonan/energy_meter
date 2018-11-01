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
#include "buckler.h"

#include "../../libraries/grove_light/tsl2561.h"

//Variables to maintain thresholds
bool update_thresh = false;
float upper;
float lower;

//Macro for defining a TWI instance
//Param 1: instance name; Param 2: queue size; Param 3: index
NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

//This function will be called on the lux data after a transaction is complete
//Not sure if we ever want to read... just want to register an interrupt
static void sensor_read_callback(float lux){
	//For now this is used once on startup to peek at the ambient light values, and set thresholds based on those values
	upper = lux + lux*0.10;
	lower = lux - lux*0.10;

	update_thresh = true;
	printf("LUX VALUE (x1000): %f\n", lux);
}

//This function will be called when the tsl2561 is interrupted
static void interrupt_callback(){
	tsl2561_schedule_read_lux();
	printf("Interrupt happened");
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
	
	//This object is documented in tsl2561.h. Configures our light sensor
	/*const tsl2561_config_t config = {
		.continuous = 0,
		.manual = 0,
		.cdr = 0,
		.int_time = 3,
	};*/

	//Not exactly sure why this is necessary yet
	nrf_delay_ms(500);

	//Series of function calls from the tsl2561 library
	tsl2561_init(&twi_mngr_instance);
	
	tsl2561_power_on(1);

	const tsl2561_config_t config = {
		.gain =  0,
		.int_time = 2,
		.int_mode = 0,
		.persist = 15,
	};

	tsl2561_config(config);

	while(1){
		tsl2561_read_lux(0);
		nrf_delay_ms(5000);
	}

	/*
	while(1){
		//All we should do is wait for an interrupt, handle it, then wait again
		//We probably need a couple more pieces here, not sure yet though
		//__WFE();
	
		if (update_thresh){
			tsl2561_set_upper_threshold(upper);
			tsl2561_set_lower_threshold(lower);
			update_thresh = false;
		}

		tsl2561_schedule_read_lux();

		//printf("Upper: %f, Lower: %f\n", upper, lower);

		nrf_delay_ms(1000);
	}*/
}
