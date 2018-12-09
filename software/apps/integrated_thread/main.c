#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nrf.h"
#include "nrf_timer.h"
#include "app_timer.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_power.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include <openthread/openthread.h>

#include "simple_thread.h"
#include "thread_coap.h"


//------------------
//Begin integrated_ble
#include "nrf_twi_mngr.h"
#include "app_util_platform.h"
#include "nordic_common.h"
//#include "nrf_drv_clock.h"
//#include "nrfx_rtc.h"
#include "nrf_power.h"
#include "nrf_drv_gpiote.h"
#include "app_error.h"
#include "buckler.h"
#include "math.h"
#include "app_util.h"
//#include "simple_ble.h"

#include "../../libraries/grove_light/tsl2561.h"
//end integrated_ble
//------------------


APP_TIMER_DEF(coap_send_timer);

//#define COAP_SERVER_ADDR "64:ff9b::22da:2eb5"
#define COAP_SERVER_ADDR "64:ff9b::23ab:a678"
//#define COAP_SERVER_ADDR "64:ff9b::ac1f:27ea"

#define LED0 NRF_GPIO_PIN_MAP(0,4)
#define LED1 NRF_GPIO_PIN_MAP(0,5)
#define LED2 NRF_GPIO_PIN_MAP(0,6)

#define DEFAULT_CHILD_TIMEOUT    40                                         /**< Thread child timeout [s]. */
#define DEFAULT_POLL_PERIOD      1000                                       /**< Thread Sleepy End Device polling period when MQTT-SN Asleep. [ms] */
#define NUM_SLAAC_ADDRESSES      4                                          /**< Number of SLAAC addresses. */


static otIp6Address m_peer_address =
{
    .mFields =
    {
        .m8 = {0}
    }
};

//----------------------------
// Begin integrated ble

//Variables to maintain thresholds
bool update_thresh = false;
unsigned int upper;
unsigned int lower;

//Global Count
static uint32_t count;

#define num_cbf 4
//static uint8_t count_buff_len = 8; //TODO: why was this here? it is the same as above
// Buffer to store uint32_t int count in an array of uint8_t
static uint8_t count_buff[num_cbf];
// Variable for storing the count


//Macro for defining a TWI instance
//Param 1: instance name; Param 2: queue size; Param 3: index
NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

// RTC0 is used by SoftDevice, RTC1 is used by app_timer
//const nrfx_rtc_t rtc_instance = NRFX_RTC_INSTANCE(0);

// Interrupt handler; currently not used not sure if needed
//static void rtc_handler(nrfx_rtc_int_type_t int_type) {}

// Convert RTC ticks to milliseconds
uint32_t rtc_to_s(uint32_t ticks) {
  return ticks / 1000;
}

// Converts a uint32_t into 4 uint8_t and places in count_buff
void update_count_buffer(void) {
  count_buff[0] = (count >> 24) & 0xFF;
  count_buff[1] = (count >> 16) & 0xFF;
  count_buff[2] = (count >> 8) & 0xFF;
  count_buff[3] = count & 0xFF;
  /*uint32_t t = nrfx_rtc_counter_get(&rtc_instance);
  count_buff[4] = (t >> 24) & 0xFF;
  count_buff[5] = (t >> 16) & 0xFF;
  count_buff[6] = (t >> 8) & 0xFF;
  count_buff[7] = t & 0xFF;*/
}

//This function will be called when the tsl2561 is interrupted
//For now, read the lux, update the thresholds accordingly and clear the interrupt.
//TODO: Send out packets to the gateway
void interrupt_handler(){
  nrf_drv_gpiote_out_toggle(14);
  printf("INTERRUPT HAPPENED!");

  printf("Lux Code 0: %i\n", tsl2561_read_lux_code0());
  //printf("Lux Code 1: %i\n", tsl2561_read_lux_code1());
  unsigned int lux0_int = tsl2561_read_lux_code0();
  unsigned int lux1_int = tsl2561_read_lux_code1();
  nrf_delay_ms(1500);
  unsigned int lux0_reg = tsl2561_read_lux_code0();
  unsigned int lux1_reg = tsl2561_read_lux_code1();
  double ratio = ((double) abs(lux0_reg - lux0_int)) / ((double) abs(lux1_reg - lux1_int));
  
  upper = lux0_reg + 50;
  lower = (lux0_reg <= 5) ? 0 : lux0_reg - 100;

  tsl2561_write_threshold_upper(upper);
  tsl2561_write_threshold_lower(lower);
  
  tsl2561_clear_interrupt();
  
  if (ratio <= 2) {
    //printf("FALSE POSITIVE!\n");
    count++;
    //update_advertisement();
    update_count_buffer(); //TODO: verify that I should do this instead of the above
    printf("ACTUAL BLINK! Count: %li\n", count);
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
    .scl    = BUCKLER_SENSORS_SCL,
    .sda    = BUCKLER_SENSORS_SDA,
    .frequency  = NRF_TWI_FREQ_400K,
    .interrupt_priority = 2,
  };
  
  //Initialize the instance and error if it is not initialized properly:
  //Param 1 points at the instance we made with "NRF_TWI..." above
  //Param 2 points at our driver configuration right above
  err_code = nrf_twi_mngr_init(&twi_mngr_instance, &twi_config);
  APP_ERROR_CHECK(err_code);
}

//TODO: verify that this isn't necessary
/*void set_thread_payload(uint8_t* buffer, uint8_t length) {
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
}*/ //TODO: deal with equivalent




//TODO: so maybe instead of having this, I just want to call update_count_buffer instead of update_advertisement, and not have a set payload
// Updates the advertisement payload
/*void update_advertisement(void) {
  update_count_buffer();
  set_thread_payload(count_buff, num_cbf);
}*/


// Function starting the internal LFCLK XTAL oscillator
static void lfclk_config(void) {
   ret_code_t err_code = nrf_drv_clock_init();
   APP_ERROR_CHECK(err_code);
   nrf_drv_clock_lfclk_request(NULL);
}

/*static void rtc_init(void) {
  //lfclk_config();
  nrfx_rtc_config_t rtc_config = NRFX_RTC_DEFAULT_CONFIG;
  rtc_config.prescaler = 32;
  ret_code_t err_code = nrfx_rtc_init( &rtc_instance, &rtc_config, rtc_handler);
  APP_ERROR_CHECK(err_code);

  nrfx_rtc_tick_enable(&rtc_instance,true);

  nrfx_rtc_enable(&rtc_instance);
}*/

// end integrated_ble
//-------------------------




/**@brief Function for initializing the nrf log module.
 */
/*static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}*/ // This was originally here but is unnecessary


void send_timer_callback() {
  uint8_t adv_buffer[num_cbf] = {0}; //TODO: this was originally static, should I have left it static??
  memcpy(adv_buffer, count_buff, num_cbf);
  //uint32_t data = 0;
  //const uint8_t* data = (uint8_t*)"hello";
  otInstance* thread_instance = thread_get_instance();
  thread_coap_send(thread_instance, OT_COAP_CODE_PUT, OT_COAP_TYPE_NON_CONFIRMABLE, &m_peer_address, "test", adv_buffer, num_cbf);
  NRF_LOG_INFO("Sent test message!");
}


int main(void) {
//#ifdef SOFTDEVICE_PRESENT
//    nrf_sdh_enable_request();
//    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
//#else
    //nrf_power_dcdcen_set(1); //TODO: see if this breaks anything
//#endif

    //------------
    //Begin integrated_ble
    ret_code_t error_code = NRF_SUCCESS;

    error_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(error_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    printf("Log initialized\n");

    //rtc_init();
    //printf("time2: %li\n", rtc_to_s(nrfx_rtc_counter_get(&rtc_instance)));

    //End integrated_ble
    //---------------------------


    //log_init(); //? - is this necessary; how is it different; oh it does exactly the same thing that the above 3 lines do


    // Initialize.
    nrf_gpio_cfg_output(LED2);
    nrf_gpio_pin_set(LED2);


    //----------------------------
    //Begin integrated_ble

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

    //End integrated_ble
    //-------------------------

    otIp6AddressFromString(COAP_SERVER_ADDR, &m_peer_address);

    thread_config_t thread_config = {
      .channel = 25,
      .panid = 0xFACE,
      .sed = true,
      .poll_period = DEFAULT_POLL_PERIOD,
      .child_period = DEFAULT_CHILD_TIMEOUT,
      .autocommission = true,
    };

    thread_init(&thread_config);
    otInstance* thread_instance = thread_get_instance();
    thread_coap_client_init(thread_instance);

    app_timer_init();
    app_timer_create(&coap_send_timer, APP_TIMER_MODE_REPEATED, send_timer_callback);
    app_timer_start(coap_send_timer, APP_TIMER_TICKS(100), NULL);

    // Enter main loop.
    while (1) {
        __WFI();
        thread_process(); //TODO: comment this and below lines out if possible
        if (NRF_LOG_PROCESS() == false) // the main point of this is power consumption I think, it contains __WFE, so that might conflict with WFI
        {
          thread_sleep();
        }
    }
}
