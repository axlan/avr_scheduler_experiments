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

#include "serial.h"

// Normally the stack grows from the end of the RAM range. Here we're allocating memory on the heap
// to use as independent stacks for our tasks. The "kernel" task will have an additional stack at
// the normal position in memory.
// We could also do this in a linker script.
#define STACK_SIZE 256
uint8_t stack1[STACK_SIZE];
uint8_t stack2[STACK_SIZE];

// We're tracking time based on timer1 which runs at F_CPU / 64.
// The casting to to avoid overflowing the integer sizes.
// Much more efficient to do this without floating point eventually.
#define MS_TO_TICKS(ms) (F_CPU / (1000.0d * 64.0d / ((double)ms)))
//#define MS_TO_TICKS(ms) (250 * ms)

// These functions are declared in helpers.s . They back up the registers and switch stacks
// between the current task and the kernel.
extern void start_task(void);
extern void suspend_task(void);

// This is what's needed to specify a task.
struct Task {
	// All the state is on the stack with it's end at this pointer.
	uint8_t* stack_pointer;
	// This is used to point to the function for the task to start at.
	void (*fun_ptr)(void);
	// The index for the serial reads.
	uint8_t serial_rx_idx;
	// This is used to schedule when the task will run next.
	uint16_t next_run;
};

// Referenced in assembly code.
volatile struct Task* current_task;

// For storing the "real" stack pointer. Referenced in assembly code.
volatile uint8_t* kernel_sp;

// Used to track which task is active.
uint8_t task_idx = 0;

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
void delay(uint16_t ticks) {
	current_task->next_run = get_time() + ticks;
	suspend_task();
}

// Check if a pointer shared between tasks has been set, and if so wait until it's cleared.
// The value of the lock is LOCK_FREE if cleared, or the task_idx of the task holding the lock.
// This doesn't need a critical section since there's no preemption.
#define LOCK_FREE 0xFF
uint8_t shared_lock = LOCK_FREE;
void get_lock(uint8_t* lock) {
	while(*lock != LOCK_FREE && *lock != task_idx) {
		// Update the next wake up time so it doesn't overflow.
		current_task->next_run = get_time();
		suspend_task();
	}
	*lock = task_idx;
}

void release_lock(uint8_t* lock) {
	*lock = LOCK_FREE;
}

void task1() {
	uint8_t in_buf[16];
	while (1) {
		// If we don't already have the lock, get it.
		if (shared_lock != task_idx) {
			USART_Send((const uint8_t*)"Task1 locking\n", 14);
			get_lock(&shared_lock);
			USART_Send((const uint8_t*)"Task1 locked\n", 13);
		}
		// If we received serial data echo it and release the lock.
		uint8_t len = USART_Read(current_task->serial_rx_idx, in_buf, 16);
		if (len) {
			USART_Send((const uint8_t*)"Task1 got: ", 11);
			USART_Send(in_buf, len);
			USART_Send((const uint8_t*)"\n", 1);
			release_lock(&shared_lock);
		}
		delay(MS_TO_TICKS(1));
	}
}

void task2() {
	while (1) {
		delay(MS_TO_TICKS(10));
		// Try to get the lock. This task is slower, so it will only get the
		// lock when Task1 releases it, then sleeps.
		USART_Send((const uint8_t*)"Task2 locking\n", 14);
		get_lock(&shared_lock);
		USART_Send((const uint8_t*)"Task2 locked\n", 13);
		USART_Rx_Clear(current_task->serial_rx_idx);
		release_lock(&shared_lock);
	}
}

// Specify the stack and functions for the tasks.
#define NUM_TASKS 2
volatile struct Task tasks[] = {{stack1 + STACK_SIZE - 1, task1, 0, 0}, {stack2 + STACK_SIZE - 1, task2, 1, 0}};

// Initialize the return pointer in the tasks' stacks.
void setup_start_funcs() {
	for (uint8_t i = 0; i < NUM_TASKS; i++) {
		// Add the function pointers to the stack. The stack grows down.
		// Most things are little endian, but this address is stored big endian: https://www.avrfreaks.net/forum/big-endian-or-little-endian-0
		*(tasks[i].stack_pointer) = (uint16_t)tasks[i].fun_ptr;
		tasks[i].stack_pointer--;
		*(tasks[i].stack_pointer) = (uint16_t)tasks[i].fun_ptr >> 8;
		tasks[i].stack_pointer--;
		// Space for preserved registers.
		tasks[i].stack_pointer -= 18;
	}
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

int main(void)
{
	// PORTB pin 0/1 output
	DDRB = 0x3;
	setup_start_funcs();
	
	// Enable timer1 in normal mode with 4us rate.
	// To do this, just set the clock source to the 1/64 prescaler.
	TCCR1B = (1 << CS11) | (1 << CS10);
	
	// Initialize the UART at 115200 baud.
	USART_Init(115200);
	
	// Enable interrupts to allow UART RX and TX IRQs.
	sei();
	
	/*
	// Some test code for the serial library.
	const uint8_t data_o[] = {1,2,3,4,5,6};
	const uint8_t data_i[] = {1,2,3,4,5,6};
	while (true) {
		USART_Send(data_o, 6);
		while (!USART_Read(0, data_i, 6)) {}
		USART_Rx_Clear(1);
	}
	*/

	while (1)
	{
		current_task = tasks + task_idx;
		if (is_time_past(current_task->next_run)) {
			// This switches to the stack for the current_task. Execution won't return here until that
			// task calls suspend_task.
			start_task();	
		}
		task_idx = (task_idx + 1) % NUM_TASKS;
	}
}
