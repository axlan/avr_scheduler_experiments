#pragma once

#include <stdbool.h>

// These functions are declared in helpers.s . They back up the registers and switch stacks
// between the current task and the kernel.
extern void start_task(void);
extern void suspend_task(void);

typedef void (*task_sig)(uint16_t);
// This is what's needed to specify a task.
struct Task {
	// All the state is on the stack with it's end at this pointer.
	uint8_t* stack_pointer;
	// This is used to point to the function for the task to start at.
	uint16_t task_offset;
	// This is used to schedule when the task will run next.
	uint16_t next_run;
	char name[16];
	uint16_t size;
	bool enabled; 
};

// Read timer1 counter.
// This timer overflows every ~4 seconds
uint16_t get_time();

// Ticks are 64us based on timer1 settings
// To keep things simple, I'm going to assume the delay times are much less than 2^15 as a way to detect overflows.
void delay_ms(uint16_t ms);

// Check if a pointer shared between tasks has been set, and if so wait until it's cleared.
// The value of the lock is LOCK_FREE if cleared, or the task_idx of the task holding the lock.
// This doesn't need a critical section since there's no preemption.
void get_lock();

// Release the lock (regardless of what task is holding it).
void release_lock();

// Returns false if `get_lock()` would block.
bool is_lock_available();

// Read the UART buffer for the currently active task.
uint8_t usart_read(void* data, uint8_t len);

// Initialize the shared function pointers.
void setup_scheduler_funcs();

// This assumes that the tasks are running for less than 125 ms, and delaying for less than 125 ms.
// This time scale could be increased either by slowing down timer1 (and losing precision) or by using a timer interrupt to make the effective timer size 32 bit.
bool is_time_past(uint16_t target_time);

// Return the pointer to the current task's name string.
const char* get_task_name(uint8_t* size);

// Have a task release any resources (like locks) it might be holding.
void cleanup_task(uint8_t idx);
