/*
 * Here I add some basic logic to include a timer for the scheduler to let tasks sleep for
 * specified amount of time.
 * Comments assume F_CPU == 16e6 (default Arduino clock)
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>

#ifndef F_CPU
	#define F_CPU 16000000
#endif


#include "scheduler_funcs.h"
#include "syscalls.h"
#include "serial.h"

// We're tracking time based on timer1 which runs at F_CPU / 64.
// The casting to to avoid overflowing the integer sizes.
// Much more efficient to do this without floating point eventually.
#define MS_TO_TICKS(ms) (F_CPU / (1000.0d * 64.0d / ((double)ms)))
//#define MS_TO_TICKS(ms) (250 * ms)


// Referenced in assembly code.
extern volatile struct Task* current_task;

// For storing the "real" stack pointer. Referenced in assembly code.
extern  volatile uint8_t* kernel_sp;

// Used to track which task is active.
extern uint8_t task_idx;

// Read timer1 counter.
// This timer overflows every ~4 seconds
inline uint16_t get_time() {
	// From datasheet: "Each 16-bit timer has a single 8-bit register for temporary storing of the
	// high byte of the 16-bit access... For a 16-bit read, the low byte must be read before the high byte." 
	uint16_t ret = TCNT1L;
	return ret | (TCNT1H << 8);
}

// Ticks are 64us based on timer1 settings
// To keep things simple, I'm going to assume the delay times are much less than 2^15 as a way to detect overflows.
void delay_ms(uint16_t ms) {
	uint16_t ticks = MS_TO_TICKS(ms);
	current_task->next_run = get_time() + ticks;
	suspend_task();
}

// Check if a pointer shared between tasks has been set, and if so wait until it's cleared.
// The value of the lock is LOCK_FREE if cleared, or the task_idx of the task holding the lock.
// This doesn't need a critical section since there's no preemption.
#define LOCK_FREE 0xFF
uint8_t shared_lock = LOCK_FREE;
void get_lock() {
	while(shared_lock != LOCK_FREE && shared_lock != task_idx) {
		// Update the next wake up time so it doesn't overflow.
		current_task->next_run = get_time();
		suspend_task();
	}
	shared_lock = task_idx;
}

void release_lock() {
	shared_lock = LOCK_FREE;
}

bool is_lock_available() {
	return shared_lock == LOCK_FREE || shared_lock == task_idx;
}

uint8_t usart_read(void* data, uint8_t len) {
	return USART_Read(task_idx + 1, data, len);
}

// Initialize the shared function pointers.
void setup_scheduler_funcs() {
	scheduler.delay_ms = delay_ms;
	scheduler.get_lock = get_lock;
	scheduler.is_lock_available = is_lock_available;
	scheduler.release_lock =release_lock;
	scheduler.usart_read = usart_read;
	scheduler.usart_write = USART_Send;
}

// This assumes that the tasks are running for less than 125 ms, and delaying for less than 125 ms.
// This time scale could be increased either by slowing down timer1 (and losing precision) or by using a timer interrupt to make the effective timer size 32 bit.
bool is_time_past(uint16_t target_time) {
	uint16_t current_time = get_time();
	const int32_t HALF_TIMER = 0x8000;
	
	int32_t diff = ((int32_t)target_time) - ((int32_t)current_time);
	
	// If target_time rolled over, but current_time hasn't
	if (diff < -HALF_TIMER) {
		return false;	
	}
	// If current_time rolled over, but target_time hasn't
	else if(diff > HALF_TIMER) {
		return true;
	}
	else {
		return diff < 0;
	}	
}
