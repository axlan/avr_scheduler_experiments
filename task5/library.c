/*
 * task3.c
 *
 * Created: 5/27/2022 3:34:58 AM
 * Author : feros
 */ 

#include <avr/io.h>
#include <avr/pgmspace.h>

#include "scheduler_funcs.h"

const char LOCKING_STR[] PROGMEM = "Task1 locking\n";
const char LOCKED_STR[] PROGMEM = "Task1 locked\n";
const char GOT_STR[] PROGMEM = "\nGot by Task1\n";

#define SEND_P_STR(buf, str) \
	strcpy_P(buf, str); \
	scheduler.usart_write(buf, sizeof(str) - 1)
	

TASK_ENTRY
void task()  {
	char in_buf[16];
	while (1) {
		// If we don't already have the lock, get it.
		if (!scheduler.is_lock_available()) {
			SEND_P_STR(in_buf, LOCKING_STR);
			scheduler.get_lock();
			SEND_P_STR(in_buf, LOCKED_STR);
		}
		// If we received serial data echo it and release the lock.
		uint8_t len = scheduler.usart_read(in_buf, 16);
		if (len) {
			scheduler.usart_write(in_buf, len);
			SEND_P_STR(in_buf, GOT_STR);
			scheduler.release_lock();
		}
		scheduler.delay_ms(1);
	}
}
