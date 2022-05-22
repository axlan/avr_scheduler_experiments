/*
 * Here I add some basic logic to include a timer for the scheduler to let tasks sleep for
 * specified amount of time.
 * Comments assume F_CPU == 1e6
 */

#include <avr/io.h>
#include <math.h>
#include <stdbool.h>

#ifndef F_CPU
	#define F_CPU 1000000
#endif

// Normally the stack grows from the end of the RAM range. Here we're allocating memory on the heap
// to use as independent stacks for our tasks. The "kernel" task will have an additional stack at
// the normal position in memory.
// We could also do this in a linker script.
#define STACK_SIZE 512
uint8_t stack1[STACK_SIZE];
uint8_t stack2[STACK_SIZE];

// We're tracking time based on timer1 which runs at F_CPU / 64.
// The casting to to avoid overflowing the integer sizes.
#define MS_TO_TICKS(ms) (F_CPU / (1000ull * 64ull / ((uint32_t)ms)))

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
	// This is used to schedule when the task will run next.
	uint16_t next_run;
};

// Referenced in assembly code.
volatile struct Task* current_task;

// For storing the "real" stack pointer. Referenced in assembly code.
volatile uint8_t* kernel_sp;

// Used to track which task is active.
int task_idx = 0;

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

// Dummy tasks to run.
void task1() {
	while (1) {
		// Note that both tasks are modifying PORTB. This would not be safe if preemption was a possibility.
		PORTB |= 1;
		delay(MS_TO_TICKS(200));
		PORTB &= ~1;
		delay(MS_TO_TICKS(100));
	}
}

void task2() {
	while (1) {
		PORTB |= 2;
		delay(MS_TO_TICKS(100));
		PORTB &= ~2;
		delay(MS_TO_TICKS(200));
	}
}

// Specify the stack and functions for the tasks.
#define NUM_TASKS 2
volatile struct Task tasks[] = {{stack1 + STACK_SIZE - 1, task1, 0}, {stack2 + STACK_SIZE - 1, task2, 0}};

// Initialize the return pointer in the tasks' stacks.
void setup_start_funcs() {
	for (uint8_t i = 0; i < NUM_TASKS; i++) {
		// Add the function pointers to the stack. The stack grows down.
		// Most things are little endian, but this address is stored big endian: https://www.avrfreaks.net/forum/big-endian-or-little-endian-0
		*(tasks[i].stack_pointer) = (uint16_t)tasks[i].fun_ptr;
		tasks[i].stack_pointer--;
		*(tasks[i].stack_pointer) = (uint16_t)tasks[i].fun_ptr >> 8;
		tasks[i].stack_pointer--;
	}
}

// This assumes that the tasks are running for less than 2 seconds, and delaying for less than 2 seconds.
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
	
	// Enable timer1 in normal mode with 64us rate.
	// To do this, just set the clock source to the 1/64 prescaler.
	TCCR1B = (1 << CS11) | (1 << CS10);
	
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
