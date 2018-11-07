// Robot Template app
//
// Framework for creating applications that control the Kobuki robot

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_error.h"
#include "app_timer.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_drv_spi.h"

#include "buckler.h"
#include "display.h"
#include "kobukiActuator.h"
#include "kobukiSensorPoll.h"
#include "kobukiSensorTypes.h"
#include "kobukiUtilities.h"
#include "mpu9250.h"

// I2C manager
NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

typedef enum {
	OFF,
	DRIVING,
	TURNING,
	BACK_UP,
	ALIGN_WITH_SLOPE
} robot_state_t;

typedef enum {
	TURN_LEFT_45,
	TURN_RIGHT_45,
	TURN_RIGHT_90,
	TURN_180
} turn_state_t;

typedef enum {
	ALIGN_UP_SLOPE,
	ALIGN_DOWN_SLOPE
} align_direction_t;

typedef struct {
	float theta;
	float psi;
	float phi;
} orientation;

typedef struct {
	bool left;
	bool rightCenter;
} bump_t; 

typedef struct {
	bool left;
	bool center;
	bool rightCenter;
} cliff_t;

static float measure_distance(uint16_t current_encoder, uint16_t previous_encoder) {
	const float CONVERSION = 0.00008529;
	if (current_encoder >= previous_encoder) {
		return CONVERSION * (current_encoder - previous_encoder);
	} else {
		return -CONVERSION * (previous_encoder - current_encoder);//return (CONVERSION * ((uint16_t)(~0) - previous_encoder + current_encoder));
	}
}

static orientation read_tilt(void) {
	mpu9250_measurement_t measurement = mpu9250_read_accelerometer();
	float x = measurement.x_axis;
	float y = measurement.y_axis;
	float z = measurement.z_axis;
	float theta = atan2(x, sqrt(y*y + z*z)) * 180 / 3.1415; //x-axis
	float psi = atan2(y, sqrt(x*x + z*z)) * 180 / 3.1415; //y-axis
	float phi = atan2(sqrt(x*x + y*y), z) * 180 / 3.1415; //z-axis
	// printf("theta = %f, psi = %f, phi = %f\n", theta, psi, phi);
	orientation ret;
	ret.theta = theta;
	ret.psi = psi;
	ret.phi = phi;
	return ret;
}

static bump_t update_bump(KobukiSensors_t sensors){
	bump_t temp;
	temp.left = sensors.bumps_wheelDrops.bumpLeft;
	temp.rightCenter = sensors.bumps_wheelDrops.bumpRight || sensors.bumps_wheelDrops.bumpCenter;
	return temp;
}

static cliff_t update_cliff(KobukiSensors_t sensors){
	cliff_t temp;
	temp.left = sensors.cliffLeft;
	temp.center = sensors.cliffCenter;
	temp.rightCenter = sensors.cliffRight && sensors.cliffRightSignal < 500;// || sensors.cliffCenter;
	return temp;
}

int main(void) {
	ret_code_t error_code = NRF_SUCCESS;

	// initialize RTT library
	error_code = NRF_LOG_INIT(NULL);
	APP_ERROR_CHECK(error_code);
	NRF_LOG_DEFAULT_BACKENDS_INIT();
	printf("Log initialized!\n");

	// initialize LEDs
	nrf_gpio_pin_dir_set(23, NRF_GPIO_PIN_DIR_OUTPUT);
	nrf_gpio_pin_dir_set(24, NRF_GPIO_PIN_DIR_OUTPUT);
	nrf_gpio_pin_dir_set(25, NRF_GPIO_PIN_DIR_OUTPUT);

	// initialize display
	nrf_drv_spi_t spi_instance = NRF_DRV_SPI_INSTANCE(1);
	nrf_drv_spi_config_t spi_config = {
		.sck_pin = BUCKLER_LCD_SCLK,
		.mosi_pin = BUCKLER_LCD_MOSI,
		.miso_pin = BUCKLER_LCD_MISO,
		.ss_pin = BUCKLER_LCD_CS,
		.irq_priority = NRFX_SPI_DEFAULT_CONFIG_IRQ_PRIORITY,
		.orc = 0,
		.frequency = NRF_DRV_SPI_FREQ_4M,
		.mode = NRF_DRV_SPI_MODE_2,
		.bit_order = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST
	};
	error_code = nrf_drv_spi_init(&spi_instance, &spi_config, NULL, NULL);
	APP_ERROR_CHECK(error_code);
	display_init(&spi_instance);
	display_write("Hello, Human!", DISPLAY_LINE_0);
	printf("Display initialized!\n");

	// initialize i2c master (two wire interface)
	nrf_drv_twi_config_t i2c_config = NRF_DRV_TWI_DEFAULT_CONFIG;
	i2c_config.scl = BUCKLER_SENSORS_SCL;
	i2c_config.sda = BUCKLER_SENSORS_SDA;
	i2c_config.frequency = NRF_TWIM_FREQ_100K;
	error_code = nrf_twi_mngr_init(&twi_mngr_instance, &i2c_config);
	APP_ERROR_CHECK(error_code);
	mpu9250_init(&twi_mngr_instance);
	printf("IMU initialized!\n");

	// initialize Kobuki
	kobukiInit();
	printf("Kobuki initialized!\n");

	// configure initial state
	robot_state_t state = OFF;
	KobukiSensors_t sensors = {0};

	uint16_t prevLeftEncoder = 0;
	uint16_t prevRightEncoder = 0;
	float rightDistance = 0; 
	float angle = 0;
	float targetAngle = 0;
	float direction = 1;
	turn_state_t turnState = TURN_RIGHT_90;
	float alignDirection = 1;
	bool hitBump = false;

	// loop forever, running state machine
	while (1) {
		// read sensors from robot
		kobukiSensorPoll(&sensors);
		orientation currOrientation = read_tilt();
			// printf("%f %f %f\n", currOrientation.theta, currOrientation.psi, currOrientation.phi);
		bump_t bump = update_bump(sensors);
		cliff_t cliff = update_cliff(sensors);

		// delay before continuing
		// Note: removing this delay will make responses quicker, but will result
		//  in printf's in this loop breaking JTAG
		// nrf_delay_ms(50);

		// handle states
		switch(state) {
			case OFF: {
				// transition logic
				if (is_button_pressed(&sensors)) {
					alignDirection = 1;
					state = ALIGN_WITH_SLOPE;
					rightDistance = 0;
					angle = 0;
					prevLeftEncoder = sensors.leftWheelEncoder;
					prevRightEncoder = sensors.rightWheelEncoder;
				} else {
					// perform state-specific actions here
					display_write("OFF", DISPLAY_LINE_0);
					kobukiDriveDirect(0, 0);
					state = OFF;
				}
				break; // each case needs to end with break!
			}      

			case DRIVING: {
			    // transition logic
			    if (is_button_pressed(&sensors)) {
					state = OFF;
			    } else if (cliff.left || cliff.rightCenter || bump.left || bump.rightCenter) {
			    // } else if ((cliff.left || cliff.rightCenter) ) {
			    			// && !(cliff.center && abs(currOrientation.theta) > 10)) {
			    	//deal with bump or cliff
					state = BACK_UP;
					if (cliff.left) {
						// turnState = TURN_RIGHT_45;
						targetAngle = 25;
						direction = 1;
					} else if (bump.left || bump.rightCenter) {
							targetAngle = 175;
							direction = 1;
							if (hitBump) {
								state = OFF;
							} else {
								hitBump = true;
							}
					} else {
						// turnState = TURN_LEFT_45;
						targetAngle = -25;
						direction = -1;
					}
					angle = 0;
					rightDistance = 0;
					prevRightEncoder = sensors.rightWheelEncoder;
			  //   } else if ((bump.left || bump.rightCenter)) {
			  //   	//reaching top/bottom of the hill
					// state = BACK_UP;
			  //     	// turnState = TURN_180;
			  //     	targetAngle = 175;
			  //     	direction = 1;
			  //     	angle = 0;
			  //     	mpu9250_start_gyro_integration();
			    } else {
			        // driving protocol
			        state = DRIVING;
				    float tempDistance = measure_distance(sensors.rightWheelEncoder, prevRightEncoder);
				    if (abs(tempDistance-rightDistance)<.3){
				    	rightDistance = tempDistance;
				    }
				    kobukiDriveDirect(250, 250);
				    display_write("DRIVING", DISPLAY_LINE_0);
			        char buf[16];
				    snprintf(buf, 16, "%i", sensors.cliffRightSignal);
				    display_write(buf, DISPLAY_LINE_1);
			    }
			    break; // each case needs to end with break!
			}

			case TURNING: {
				if (is_button_pressed(&sensors) ) {
					state = OFF;
					mpu9250_stop_gyro_integration();
				} else if (cliff.left || cliff.rightCenter || bump.left || bump.rightCenter) {
			    	//deal with bump or cliff
			    	state = BACK_UP;
					if (cliff.left || bump.left) {
						// turnState = TURN_RIGHT_45;
						targetAngle = 25;
						direction = 1;
					} else {
						// turnState = TURN_LEFT_45;
						targetAngle = -25;
						direction = -1;
					}
					mpu9250_stop_gyro_integration();
					angle = 0;
					rightDistance = 0;
					prevRightEncoder = sensors.rightWheelEncoder;
			    } else if (abs(angle) > abs(targetAngle)) {
			    	//target angle reached
					state = DRIVING;
					mpu9250_stop_gyro_integration();
					angle = 0;
					rightDistance = 0;
					prevRightEncoder = sensors.rightWheelEncoder;
				} else {
					angle = - mpu9250_read_gyro_integration().z_axis;
					if (abs(targetAngle - angle) > 20) {
						kobukiDriveDirect(direction*100, direction*-100);
					} else {
						kobukiDriveDirect(direction*25, direction*-25);
					}	
					state = TURNING;
					display_write("TURNING", DISPLAY_LINE_0);
					char buf[16];
				    snprintf(buf, 16, "%i", sensors.cliffRightSignal);
				    display_write(buf, DISPLAY_LINE_1);
				}
			    break; // each case needs to end with break!
		    }
		  	case BACK_UP: {
			    // transition logic
			    if (is_button_pressed(&sensors)) {
			      	state = OFF;
				} else if (rightDistance < -0.2) {
					// state = ALIGN_WITH_SLOPE;
					// angle = 0;
					state = TURNING;
			      	angle = 0;
			      	mpu9250_start_gyro_integration();
				} else {
				  // perform state-specific actions here
				    rightDistance = measure_distance(sensors.rightWheelEncoder, prevRightEncoder);
				    kobukiDriveDirect(-100, -100);
				    printf("%f\n", rightDistance);
				  	display_write("BACK_UP", DISPLAY_LINE_0);
					char buf[16];
					snprintf(buf, 16, "%f", rightDistance);
					display_write(buf, DISPLAY_LINE_1);
				}
				break; // each case needs to end with break!
			}
			case ALIGN_WITH_SLOPE: {
				printf("%f %f\n", currOrientation.theta, currOrientation.psi);

				if (is_button_pressed(&sensors) ) {
					state = OFF;
				} else if (cliff.left || cliff.rightCenter || bump.left || bump.rightCenter) {
					state = BACK_UP;
					if (cliff.left || bump.left) {
						// turnState = TURN_RIGHT_45;
						targetAngle = 25;
						direction = 1;
					} else {
						// turnState = TURN_LEFT_45;
						targetAngle = -25;
						direction = -1;
					}
					angle = 0;
					rightDistance = 0;
					prevRightEncoder = sensors.rightWheelEncoder;
				} else if (abs(currOrientation.psi) < 0.05 && (alignDirection*currOrientation.theta < 0 )) {
					//stop turning and go to drive state
					printf("%f\n", currOrientation.psi);
					state = DRIVING;
					angle = 0;
					rightDistance = 0;
					prevRightEncoder = sensors.rightWheelEncoder;
				} else {
					//keep turning
					// if (abs(currOrientation.psi) > 1.5) {
					// 	kobukiDriveDirect(50, -50);
					// } else {
					// 	kobukiDriveDirect(20, -20);
					// }	
					kobukiDriveDirect(20, -20);				 
					state = ALIGN_WITH_SLOPE;
					display_write("ALIGNING", DISPLAY_LINE_0);
				    char buf[16];
				    snprintf(buf, 16, "%f", currOrientation.psi);
				    display_write(buf, DISPLAY_LINE_1);
				}
			    break; // each case needs to end with break!
			}
		}
	}
}

