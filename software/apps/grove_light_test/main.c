// BLE TX app
//
// Sends BLE advertisements with data

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "app_util.h"
#include "simple_ble.h"
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

// Number of 8bit ints in desired count size
#define num_cbf 4
static uint8_t count_buff_len = 4;
// Buffer to store uint32_t int count in an array of uint8_t
static uint8_t count_buff[num_cbf];
// Variable for storing the count
static uint32_t count;



// Create a timer
APP_TIMER_DEF(adv_timer);

//Macro for defining a TWI instance
//Param 1: instance name; Param 2: queue size; Param 3: index
NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

//Global variables for grove light thresholds
bool update_thresh = false;
unsigned int upper;
unsigned int lower;
unsigned int global_count;

//Interrupt Handler for the grove light app
void interrupt_handler(){
  printf("interrupt");
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
	
	upper = lux0_reg + 100;
	lower = (lux0_reg <= 5) ? 0 : lux0_reg - 100;

	tsl2561_write_threshold_upper(upper);
	tsl2561_write_threshold_lower(lower);
	
	tsl2561_clear_interrupt();
	
	if (ratio <= 2) {
		//printf("FALSE POSITIVE!\n");
		count++;
		printf("ACTUAL BLINK! Count: %i\n", global_count);
	//} else {
	}

	//printf("New Upper: %i, New Lower: %i\n", upper, lower);
	//printf("Ratio: %f\n", ratio);
}

//Configurationg for the interrupt handler
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

//twi initialization
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


// BLE configuration
static simple_ble_config_t ble_config = {
        // BLE address is c0:98:e5:49:00:03
        .platform_id       = 0x49,    // used as 4th octet in device BLE address
        .device_id         = 0x0003,  // used as the 5th and 6th octet in the device BLE address, you will need to change this for each device you have
        .adv_name          = "EE149", // irrelevant in this example
        .adv_interval      = MSEC_TO_UNITS(1000, UNIT_0_625_MS), // send a packet once per second (minimum is 20 ms)
        .min_conn_interval = MSEC_TO_UNITS(500, UNIT_1_25_MS), // irrelevant if advertising only
        .max_conn_interval = MSEC_TO_UNITS(1000, UNIT_1_25_MS), // irrelevant if advertising only
};
simple_ble_app_t* simple_ble_app;



// Sends the specified data over BLE advertisements
void set_ble_payload(uint8_t* buffer, uint8_t length) {
  static uint8_t adv_buffer[24] = {0};
  static ble_advdata_manuf_data_t adv_payload = {
    .company_identifier = 0x02E0, // Lab11 company ID (University of Michigan)
    .data.p_data = adv_buffer,
    .data.size = 24,
  };

  // copy over up to 23 bytes of advertisement payload
  adv_buffer[0] = 0x23; // identifies a Buckler advertisement payload
  if (length > 23) {
    length = 23; // maximum size is 23 bytes of payload
  }
  memcpy(&(adv_buffer[1]), buffer, length);
  adv_payload.data.size = 1+length;

  // create an advertisement with a manufacturer-specific data payload
  // https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.sdk5.v15.0.0%2Fstructble__advdata__t.html
  ble_advdata_t advdata = {0};
  advdata.name_type = BLE_ADVDATA_NO_NAME; // do not include device name (adv_name) in advertisement
  advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE; // BLE Low energy advertisement
  advdata.p_manuf_specific_data = &adv_payload;

  // update advertisement data and start advertising
  simple_ble_set_adv(&advdata, NULL);
}

// Converts a uint32_t into 4 uint8_t and places in count_buff
void update_count_buffer(void) {
  count_buff[0] = (count >> 24) & 0xFF;
  count_buff[1] = (count >> 16) & 0xFF;
  count_buff[2] = (count >> 8) & 0xFF;
  count_buff[3] = count & 0xFF;
}

// Callback when the timer fires. Updates the advertisement data
void adv_timer_callback(void) {
  // Update advertisement data
  // Increments each value by two each time
  // This is just temporary until we get interrupts working on the sensor
  count += 2;
  update_count_buffer();
  set_ble_payload(count_buff, count_buff_len);
}

// Updates the advertisement payload
void update_advertisement(void) {
	count += 1;
  update_count_buffer();
	set_ble_payload(count_buff, count_buff_len);
}


int main(void) {

  // Setup BLE
  // Note: simple BLE is our own library. You can find it in `nrf5x-base/lib/simple_ble/`
  simple_ble_app = simple_ble_init(&ble_config);

  update_advertisement();

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

  configure_interrupt(BUCKLER_BUTTON0, interrupt_handler);

  // Set a timer to change the data. Data could also be changed after sensors
  // are read in real applications
  //app_timer_init();
  //app_timer_create(&adv_timer, APP_TIMER_MODE_REPEATED, (app_timer_timeout_handler_t)adv_timer_callback);
  //app_timer_start(adv_timer, APP_TIMER_TICKS(1000), NULL); // 1000 milliseconds

  // go into low power mode
  while(1) {
   // power_manage();
    printf("Lux Code 0: %i\n", tsl2561_read_lux_code0());
		printf("Lux Code 1: %i\n", tsl2561_read_lux_code1());

  }
}

