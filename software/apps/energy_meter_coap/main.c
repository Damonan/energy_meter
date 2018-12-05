#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "simple_thread.h"
#include "thread_coap.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_twi_mngr.h"
#include "app_util_platform.h"
#include "nordic_common.h"
#include "app_timer.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_gpiote.h"
#include "nrf_drv_saadc.h"
#include "nrf_power.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include <openthread/openthread.h>
#include <openthread/message.h>


#include "energy_meter_coap.h"

#include "device_id.h"

//#include "ntp.h" 
#include "buckler.h"
//#include "ab1815.h"

// #include "max44009.h"





#define COAP_SERVER_ADDR "64:ff9b::22da:2eb5" //TODO: change this
//#define NTP_SERVER_ADDR "64:ff9b::8106:f1c"
#define PARSE_ADDR "j2x.us/perm"

#define DEFAULT_CHILD_TIMEOUT    40   /**< Thread child timeout [s]. */
#define DEFAULT_POLL_PERIOD      5000 /**< Thread Sleepy End Device polling period when Asleep. [ms] */
#define RECV_POLL_PERIOD         100  /**< Thread Sleepy End Device polling period when expecting response. [ms] */
#define NUM_SLAAC_ADDRESSES      6    /**< Number of SLAAC addresses. */

static uint8_t device_id[6];
static otNetifAddress m_slaac_addresses[6]; /**< Buffer containing addresses resolved by SLAAC */
//static struct ntp_client_t ntp_client;

/*static otIp6Address m_ntp_address =
{
    .mFields =
    {
        .m8 = {0}
    }
};*/
static otIp6Address m_peer_address =
{
    .mFields =
    {
        .m8 = {0}
    }
};

static buckler_packet_t packet = {
    .id = NULL,
    .id_len = 0,
    .data = NULL,
    .data_len = 0,
};

typedef enum {
  IDLE = 0,
  //SEND_LIGHT,
  //SEND_MOTION,
  SEND_PERIODIC,
  SEND_DISCOVERY,
  //UPDATE_TIME,
} buckler_state_t;

static buckler_state_t state = IDLE;
//static float sensed_lux;
static bool do_reset = false;

#define DISCOVER_PERIOD     APP_TIMER_TICKS(5*60*1000)
#define SENSOR_PERIOD       APP_TIMER_TICKS(30*1000)
//#define RTC_UPDATE_FIRST    APP_TIMER_TICKS(5*1000)

APP_TIMER_DEF(discover_send_timer);
APP_TIMER_DEF(periodic_sensor_timer);
//APP_TIMER_DEF(rtc_update_first);

//NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0); will need for light sensor
//static nrf_drv_spi_t spi_instance = NRF_DRV_SPI_INSTANCE(1);

/*void ntp_recv_callback(struct ntp_client_t* client) {
  if (client->state == NTP_CLIENT_RECV) {
    ab1815_set_time(unix_to_ab1815(client->tv));
    NRF_LOG_INFO("ntp time: %lu.%lu", client->tv.tv_sec, client->tv.tv_usec);
  }
  else {
    NRF_LOG_INFO("ntp error state: 0x%x", client->state);
  }
  otLinkSetPollPeriod(thread_get_instance(), DEFAULT_POLL_PERIOD);
}*/

/*static inline void rtc_update_callback() {
  state = UPDATE_TIME;
}*/

void __attribute__((weak)) thread_state_changed_callback(uint32_t flags, void * p_context) {
    NRF_LOG_INFO("State changed! Flags: 0x%08lx Current role: %d",
                 flags, otThreadGetDeviceRole(p_context));

    if (flags & OT_CHANGED_THREAD_NETDATA)
    {
        /**
         * Whenever Thread Network Data is changed, it is necessary to check if generation of global
         * addresses is needed. This operation is performed internally by the OpenThread CLI module.
         * To lower power consumption, the examples that implement Thread Sleepy End Device role
         * don't use the OpenThread CLI module. Therefore otIp6SlaacUpdate util is used to create
         * IPv6 addresses.
         */
         otIp6SlaacUpdate(thread_get_instance(), m_slaac_addresses,
                          sizeof(m_slaac_addresses) / sizeof(m_slaac_addresses[0]),
                          otIp6CreateRandomIid, NULL);
    }
    if (flags & OT_CHANGED_IP6_ADDRESS_ADDED && otThreadGetDeviceRole(p_context) == 2) {
      NRF_LOG_INFO("We have internet connectivity!");
      //int err_code = app_timer_start(rtc_update_first, RTC_UPDATE_FIRST, NULL); TODO: was this important?
      //APP_ERROR_CHECK(err_code);
    }
}

static inline void discover_send_callback() {
  state = SEND_DISCOVERY;
}

//so this is useless right now, but we don't want ab1815
static void tickle_or_nah(otError error) {
  if (error == OT_ERROR_NONE && otThreadGetDeviceRole(thread_get_instance()) == 2) {
    //ab1815_tickle_watchdog(); //TODO: figure out what this is
  }
}

static void send_free_buffers(void) {
  otBufferInfo buf_info = {0};
  otMessageGetBufferInfo(thread_get_instance(), &buf_info);
  printf("Buffer info:\n");
  printf("total buffers: %u\n", buf_info.mTotalBuffers);
  printf("free buffers: %u\n", buf_info.mFreeBuffers);
  printf("Ip6 buffers: %u\n", buf_info.mIp6Buffers);
  printf("Ip6 messages: %u\n", buf_info.mIp6Messages);
  printf("coap buffers: %u\n", buf_info.mCoapBuffers);
  printf("coap messages: %u\n", buf_info.mCoapMessages);
  printf("app coap buffers: %u\n", buf_info.mApplicationCoapBuffers);
  printf("app coap messages: %u\n", buf_info.mApplicationCoapMessages);
  //packet.timestamp = ab1815_get_time_unix();
  packet.data = (uint8_t*)&buf_info.mFreeBuffers;
  packet.data_len = sizeof(sizeof(uint16_t));
  tickle_or_nah(coap_send(&m_peer_address, "free_ot_buffers", false, &packet));
}


static void periodic_sensor_read_callback() {
  /*ab1815_time_t time;
  time = unix_to_ab1815(packet.timestamp);
  NRF_LOG_INFO("time: %d:%02d:%02d, %d/%d/20%02d", time.hours, time.minutes, time.seconds, time.months, time.date, time.years);
  if(time.years == 0) {
    NRF_LOG_INFO("VERY INVALID TIME");
    int err_code = app_timer_start(rtc_update_first, RTC_UPDATE_FIRST, NULL);
    APP_ERROR_CHECK(err_code);
  }*/
  //NRF_LOG_INFO("periodic"); This probably isn't actually necessary
  state = SEND_PERIODIC;
}

/*static inline void light_sensor_read_callback(float lux) {
  sensed_lux = lux;
  state = SEND_LIGHT;
}*/


//This isn't necessary now, but the structure may be useful when interrupts are added back in later
/*
static void light_interrupt_callback(void) {
    max44009_schedule_read_lux();
}*/

/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

static void timer_init(void)
{
  uint32_t err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);

  err_code = app_timer_create(&discover_send_timer, APP_TIMER_MODE_REPEATED, discover_send_callback);
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_start(discover_send_timer, DISCOVER_PERIOD, NULL);
  APP_ERROR_CHECK(err_code);

  err_code = app_timer_create(&periodic_sensor_timer, APP_TIMER_MODE_REPEATED, periodic_sensor_read_callback);
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_start(periodic_sensor_timer, SENSOR_PERIOD, NULL);
  APP_ERROR_CHECK(err_code);

  //err_code = app_timer_create(&rtc_update_first, APP_TIMER_MODE_SINGLE_SHOT, rtc_update_callback);
  //APP_ERROR_CHECK(err_code);

}

/*
void twi_init(void) {
  ret_code_t err_code;

  const nrf_drv_twi_config_t twi_config = {
    .scl                = I2C_SCL,
    .sda                = I2C_SDA,
    .frequency          = NRF_TWI_FREQ_400K,
  };

  err_code = nrf_twi_mngr_init(&twi_mngr_instance, &twi_config);
  APP_ERROR_CHECK(err_code);
}*/


//void app_error_fault_handler(uint32_t error_code, __attribute__ ((unused)) uint32_t line_num, __attribute__ ((unused)) uint32_t info) {
//  NRF_LOG_INFO("App error: %d", error_code);
//  uint8_t data[1 + 6 + sizeof(uint32_t)];
//  data[0] = 6;
//  memcpy(data+1, device_id, 6);
//  memcpy(data+1+6, &error_code, sizeof(uint32_t));
//
//  thread_coap_send(thread_get_instance(), OT_COAP_CODE_PUT, OT_COAP_TYPE_NON_CONFIRMABLE, &m_peer_address, "error", data, 1+6+sizeof(uint32_t));
//
//  do_reset = true;
//}
/*
void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
  // halt all existing state
  __disable_irq();
  NRF_LOG_FINAL_FLUSH();

  // print banner
  printf("\n\n***** App Error *****\n");

  // check cause of error
  switch (id) {
    case NRF_FAULT_ID_SDK_ASSERT: {
        assert_info_t * p_info = (assert_info_t *)info;
        printf("ASSERTION FAILED at %s:%u\n",
            p_info->p_file_name,
            p_info->line_num);
        break;
      }
    case NRF_FAULT_ID_SDK_ERROR: {
        error_info_t * p_info = (error_info_t *)info;
        printf("ERROR %lu [%s] at %s:%lu\t\tPC at: 0x%08lX\n",
            p_info->err_code,
            nrf_strerror_get(p_info->err_code),
            p_info->p_file_name,
            p_info->line_num,
            pc);
        break;
      }
    default: {
      printf("UNKNOWN FAULT at 0x%08lX\n", pc);
      break;
    }
  }


  //uint8_t data[1 + 6 + sizeof(uint32_t)];
  //data[0] = 6;
  //memcpy(data+1, device_id, 6);
  //memcpy(data+1+6, &error_code, sizeof(uint32_t));

  //thread_coap_send(thread_get_instance(), OT_COAP_CODE_PUT, OT_COAP_TYPE_NON_CONFIRMABLE, &m_peer_address, "error", data, 1+6+sizeof(uint32_t));

  do_reset = true;
}*/ //permamote has this function, but a version is in a buckler library already, it won't compile with this uncommented

void state_step(void) {
 
  switch(state) {
    case SEND_PERIODIC: {
      printf("beginning of send_periodic\n");
      send_free_buffers(); // IDK if this is necessary, but the other SEND_PERIODIC
      uint8_t data = 1;
      
      
      //packet.timestamp = ab1815_get_time_unix();
      packet.data = &data;
      packet.data_len = sizeof(data);
      tickle_or_nah(coap_send(&m_peer_address, "data", true, &packet)); // TODO: figure out whether to do false (confirmable) since the periodic sends did
      NRF_LOG_INFO("Sending data");
      state = IDLE;
      break;
    }
    /*case UPDATE_TIME: {
      NRF_LOG_INFO("RTC UPDATE");
      NRF_LOG_INFO("sent ntp poll!");
      int error = ntp_client_begin(thread_get_instance(), &ntp_client, &m_ntp_address, 123, 127, ntp_recv_callback, NULL);
      NRF_LOG_INFO("error: %d", error);
      if (error) {
        memset(&ntp_client, 0, sizeof(struct ntp_client_t));
        otLinkSetPollPeriod(thread_get_instance(), DEFAULT_POLL_PERIOD);
        return;
      }
      otLinkSetPollPeriod(thread_get_instance(), RECV_POLL_PERIOD);

      state = IDLE;
      break;
    }*/
    case SEND_DISCOVERY: {
      const char* addr = PARSE_ADDR;
      uint8_t addr_len = strlen(addr);
      uint8_t data[addr_len + 1];

      NRF_LOG_INFO("Sent discovery");

      data[0] = addr_len;
      memcpy(data+1, addr, addr_len);
      //packet.timestamp = ab1815_get_time_unix();
      packet.data = data;
      packet.data_len = addr_len + 1;

      tickle_or_nah(coap_send(&m_peer_address, "discovery", false, &packet));

      state = IDLE;
      break;
    }
    default:
      break;
  }

  if (do_reset) {
    static volatile int i = 0;
    if (i++ > 100) {
      NVIC_SystemReset();
    }
  }
}

int main(void) {
  // init softdevice
  //nrf_sdh_enable_request();
  //sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
  nrf_power_dcdcen_set(1); // not sure what this is


  // Init log
  log_init();

  // Init twi
  //twi_init(); will need for light sensor later
  //adc_init(); // figure out if necessary

  // init periodic timers
  timer_init();

  get_device_id(device_id);
  packet.id = device_id;
  packet.id_len = sizeof(device_id);

  // setup thread
  otIp6AddressFromString(COAP_SERVER_ADDR, &m_peer_address);
  ///otIp6AddressFromString(NTP_SERVER_ADDR, &m_ntp_address);
  thread_config_t thread_config = {
    .channel = 25,
    .panid = 0xFACE,
    .sed = true,
    .poll_period = DEFAULT_POLL_PERIOD,
    .child_period = DEFAULT_CHILD_TIMEOUT,
    .autocommission = true,
  };


  ////nrf_gpio_cfg_output(LI2D_CS); i don't think we need this
  //nrf_gpio_cfg_output(SPI_MISO);
  //nrf_gpio_cfg_output(SPI_MOSI);
  ////nrf_gpio_pin_set(LI2D_CS);
  //nrf_gpio_pin_set(SPI_MISO);
  //nrf_gpio_pin_set(SPI_MOSI);

  thread_init(&thread_config);
  otInstance* thread_instance = thread_get_instance();
  thread_coap_client_init(thread_instance);
  thread_process();

/* RTC stuff, probably don't need
  ab1815_init(&spi_instance);
  ab1815_control_t ab1815_config;
  ab1815_get_config(&ab1815_config);
  ab1815_config.auto_rst = 1;
  ab1815_set_config(ab1815_config);
*/
  // setup light sensor
  // const max44009_config_t config = {
  //   .continuous = 0,
  //   .manual = 0,
  //   .cdr = 0,
  //   .int_time = 3,
  // };


  // max44009_set_read_lux_callback(light_sensor_read_callback);
  // max44009_set_interrupt_callback(light_interrupt_callback);
  // max44009_config(config);
  // max44009_schedule_read_lux();
  // max44009_enable_interrupt();

/* More RTC
  ab1815_time_t alarm_time = {0};
  ab1815_set_alarm(alarm_time, ONCE_PER_DAY, (ab1815_alarm_callback*) rtc_update_callback);
  ab1815_set_watchdog(1, 15, _1_4HZ);
*/ 


  while (1) {
    thread_process();
    //ntp_client_process(&ntp_client);
    state_step(); 
    if (NRF_LOG_PROCESS() == false)
    {
      thread_sleep();
    }
  }
}