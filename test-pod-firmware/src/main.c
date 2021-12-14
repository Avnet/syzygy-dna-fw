/*
 * main.c
 *
 * Author : Opal Kelly Inc.
 * 
 *
 ********************************************************************************
 * Copyright (c) 2017 Opal Kelly Incorporated
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ********************************************************************************
 *
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include "USI_TWI_Slave.h"
#include "syzygy_helpers.h"
#include "syzygy_seq.h"
#include "syzygy_dna_fw.h"

// Assume +/- 5% as per SYZYGY spec
#define HIGH_THRESH_3v3 690 // 690 mV after resistor divider
#define LOW_THRESH_3v3  624 // 624 mV after resistor divider
// Assume +/- 10% as per SYZYGY spec
#define HIGH_THRESH_5v  1096 // 1.096 V after resistor divider
#define LOW_THRESH_5v   897  // 897 mV after resistor divider
// Assume +/- 5% as per SYZYGY spec and a 1.8V VIO
#define HIGH_THRESH_VIO 376 // 376 mV after resistor divider
#define LOW_THRESH_VIO  340 // 340 mV after resistor divider

// Number of ADC readings to average
#define ADC_READ_AVERAGES 10


// Configure the pins used by test pods
void config_test_mode_pins()
{
	DDRB = 7;
	DDRA &= ~(1 << DDA7);
}

// Perform test pod checks and communicate with FPGA
// Communication is as follows:
// If (TEST_MODE_0 high)
//    TEST_MODE_1 = 3.3V_good
//    TEST_MODE_2 = VIO_good
//    TEST_MODE_3 = 5V_good
// else
//    TEST_MODE_1 = !3.3V_good
//    TEST_MODE_2 = !VIO_good
//    TEST_MODE_3 = !5V_good
void test_pod_check()
{
	static uint8_t status_field = 0;
	static uint8_t averageIndex = 0;
	static uint16_t averageBufferSQ0[ADC_READ_AVERAGES];
	static uint16_t averageBufferSQ1[ADC_READ_AVERAGES];
	static uint16_t averageBufferSQ2[ADC_READ_AVERAGES];
	
	uint32_t adc_reading;
	
	// Read 5V ADC and check
	set_adc_mux(SQ0_ADC_MUX);
	START_ADC;
	if (averageIndex < ADC_READ_AVERAGES) {
		averageBufferSQ0[averageIndex] = read_adc();
	}
	else {
		adc_reading = 0;
		for (uint8_t i = 0; i < ADC_READ_AVERAGES; i++) {
			adc_reading += (uint32_t) averageBufferSQ0[i]; 
		}
		adc_reading /= ADC_READ_AVERAGES;
		adc_reading = adc_reading * ADC_MV;
		adc_reading = adc_reading >> ADC_BITS;
		if ((adc_reading < HIGH_THRESH_5v) && (adc_reading > LOW_THRESH_5v)) {
			status_field |= 0x1;
		}
		else
		{
			status_field &= ~0x1;
		}
	}
	
	// Read VIO ADC and check
	set_adc_mux(SQ1_ADC_MUX);
	START_ADC;
	if (averageIndex < ADC_READ_AVERAGES) {
		averageBufferSQ1[averageIndex] = read_adc();
	}
	else {
		adc_reading = 0;
		for (uint8_t i = 0; i < ADC_READ_AVERAGES; i++) {
			adc_reading += (uint32_t) averageBufferSQ1[i];
		}
		adc_reading /= ADC_READ_AVERAGES;
		adc_reading = adc_reading * ADC_MV;
		adc_reading = adc_reading >> ADC_BITS;
		if ((adc_reading < HIGH_THRESH_VIO) && (adc_reading > LOW_THRESH_VIO)) {
			status_field |= 0x2;
		}
		else
		{
			status_field &= ~0x2;
		}
	}
	
	// Read 3.3V ADC and check
	set_adc_mux(SQ2_ADC_MUX);
	START_ADC;
	if (averageIndex < ADC_READ_AVERAGES) {
		averageBufferSQ2[averageIndex] = read_adc();
	}
	else {
		adc_reading = 0;
		for (uint8_t i = 0; i < ADC_READ_AVERAGES; i++) {
			adc_reading += (uint32_t) averageBufferSQ2[i];
		}
		adc_reading /= ADC_READ_AVERAGES;
		adc_reading = adc_reading * ADC_MV;
		adc_reading = adc_reading >> ADC_BITS;
		if ((adc_reading < HIGH_THRESH_3v3) && (adc_reading > LOW_THRESH_3v3)) {
			status_field |= 0x4;
		}
		else
		{
			status_field &= ~0x4;
		}
	}
	
	// Increment current average index
	if (averageIndex >= ADC_READ_AVERAGES) {
		averageIndex = 0;
	}
	else {
		averageIndex++;
	}
	
	// Check Test Mode 0 pin and return status
	if (PINA & (1 << PINA7)) {
		PORTB = status_field & 0x7;
	} else {
		PORTB = (~status_field) & 0x7;
	}
}

int main(void)
{
	uint16_t adc_val = 0;
	uint8_t i2c_addr = 0;
	
	config_test_mode_pins();

	init_adc();
	
	// Get RGA ADC reading
	adc_val = read_adc();
	
	// Determine our I2C address if the ADC reading is valid
	if(adc_val > 0) {
		i2c_addr = adc_to_addr(adc_val);
	}
	
	// Setup I2C if we have a valid address
	if(i2c_addr > 0) {
		USI_TWI_Slave_Initialise(i2c_addr);
	}

	init_i2c_timer();
	
	// Set global interrupt enable (required for I2C communication)
	sei();
	
	// Main application loop (runs after power sequencing completes
	while (1) 
	{
		// Test pod specific functionality
		test_pod_check();
	}
}

