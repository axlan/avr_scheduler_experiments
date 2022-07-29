/*
 * task3.c
 *
 * Created: 5/27/2022 3:34:58 AM
 * Author : feros
 */ 

#include <avr/io.h>

#include "scheduler_funcs.h"

TASK_ENTRY
void task()  {
	uint8_t val = 0;
	uint8_t len = 0;
	// Set LED pin to output.
	DDRB |= 1 << 5;
	while (1) {
		while (1) {
			len = scheduler.usart_read(&val, 1);
			if (len == 0) {
				break;
			}
			if (val == '!') {
				// Toggle the LED pin.
				PORTB ^= 1 << 5;
			}
		}
		scheduler.delay_ms(100);
	}
}
