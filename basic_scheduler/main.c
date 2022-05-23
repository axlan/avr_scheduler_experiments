/**
 * The goal for this version is to have the most basic scheduler without
 * preemption.
 */

#include <avr/io.h>

// Normally the stack grows from the end of the RAM range. Here we're allocating memory on the heap
// to use as independent stacks for our tasks. The "kernel" task will have an additional stack at
// the normal position in memory.
// We could also do this in a linker script.
#define STACK_SIZE 256
uint8_t stack1[STACK_SIZE];
uint8_t stack2[STACK_SIZE];

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
};

// Dummy tasks to run.
void task1() {
	while (1) {
		// Note that both tasks are modifying PORTB. This would not be safe if preemption was a possibility.
		PORTB ^= 1;
		suspend_task();
	}
}

void task2() {
	while (1) {
		PORTB ^= 2;
		suspend_task();
	}
}

// Specify the stack and functions for the tasks.
#define NUM_TASKS 2
volatile struct Task tasks[] = {{stack1 + STACK_SIZE - 1, task1}, {stack2 + STACK_SIZE - 1, task2}};
// Referenced in assembly code.
volatile struct Task* current_task;

// For storing the "real" stack pointer. Referenced in assembly code.
volatile uint8_t* kernel_sp;

// Used to track which task is active.
int task_idx = 0;

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

int main(void)
{
	// PORTB pin 0/1 output
	DDRB = 0x3;
	setup_start_funcs();
	
    while (1) 
    {
		current_task = tasks + task_idx;
		// This switches to the stack for the current_task. Execution won't return here until that
		// task calls suspend_task.
		start_task();
		task_idx = (task_idx + 1) % NUM_TASKS;
    }
}
