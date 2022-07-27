/*
 * task3.c
 *
 * Created: 5/27/2022 3:34:58 AM
 * Author : feros
 */ 

#include <avr/io.h>
#include <avr/pgmspace.h>

#include "scheduler_funcs.h"

const char LOCKING_STR[] PROGMEM = "locking\n";
const char LOCKED_STR[] PROGMEM = "locked\n";
const char GOT_STR[] PROGMEM = "Got ";

#define SEND_P_STR_AND_NAME(str) \
    scheduler.usart_write(name, name_len); \
	memcpy_P(buffer+2, str, sizeof(str) - 1); \
	scheduler.usart_write(buffer, sizeof(str) + 1)

TASK_ENTRY
void task()  {
	// Setup for outputting name string.
	char buffer[16];
	buffer[0] = ':';
	buffer[1] = ' ';
	uint8_t name_len = 0;
	const char* name = scheduler.get_task_name(&name_len);
	

	while (1) {
		while(1) {
			// Only try to process data if TX buffer has free space.
			if (scheduler.usart_write_free() > 16) {
				SEND_P_STR_AND_NAME(LOCKING_STR);
				scheduler.get_lock();
				break;
			}
			scheduler.delay_ms(100);
		}
		while(1) {
			// Only try to process data if TX buffer has free space.
			if (scheduler.usart_write_free() > 16) {
				SEND_P_STR_AND_NAME(LOCKED_STR);
				break;
			}
			scheduler.delay_ms(100);
		}
		while(1) {
			// Only try to process data if TX buffer has free space.
			if (scheduler.usart_write_free() > 16) {
				// If we received serial data echo it and release the lock.
				uint8_t len = scheduler.usart_read(buffer+6, 9);
				if (len && buffer[6] > 31) {
					memcpy_P(buffer+2, GOT_STR, 4); 
					buffer[6 + len] = '\n';
					scheduler.usart_write(name, name_len);
					scheduler.usart_write(buffer, 6 + len + 1);
					scheduler.release_lock();
					break;
				}
			}
			scheduler.delay_ms(100);
		}
		scheduler.delay_ms(100);
	}
}
