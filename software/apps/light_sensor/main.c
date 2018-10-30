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

#include "../../libraries/light_sensor/max44009.h"
//#include "../../libraries/light_sensor/max44009.c"

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

//This function will be called when the max44009 is interrupted
static void interrupt_callback(){
	max44009_schedule_read_lux();
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
	
	//This object is documented in max44009.h. Configures our light sensor
	const max44009_config_t config = {
		.continuous = 0,
		.manual = 0,
		.cdr = 0,
		.int_time = 3,
	};

	//Not exactly sure why this is necessary yet
	nrf_delay_ms(500);

	//Series of function calls from the max44009 library
	max44009_init(&twi_mngr_instance);
	max44009_set_read_lux_callback(sensor_read_callback);
	max44009_config(config);
	//schedule_read_lux is scheduling a transaction on the twi bus
	max44009_schedule_read_lux();
	max44009_set_interrupt_callback(interrupt_callback);
	max44009_enable_interrupt();

	//Set threshold for high or low
	max44009_set_upper_threshold(upper);
	max44009_set_lower_threshold(lower);

	while(1){
		//All we should do is wait for an interrupt, handle it, then wait again
		//We probably need a couple more pieces here, not sure yet though
		//__WFE();
	
		if (update_thresh){
			max44009_set_upper_threshold(upper);
			max44009_set_lower_threshold(lower);
			update_thresh = false;
		}

		max44009_schedule_read_lux();

		//printf("Upper: %f, Lower: %f\n", upper, lower);

		nrf_delay_ms(1000);
	}
}
