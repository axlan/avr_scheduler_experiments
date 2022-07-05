/*
 * Here I add some basic logic to include a timer for the scheduler to let tasks sleep for
 * specified amount of time.
 * Comments assume F_CPU == 16e6 (default Arduino clock)
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdbool.h>

#include "syscalls.h"
#include "serial.h"

// The number of loadable tasks will be 
#ifndef MAX_TASKS
	#define MAX_TASKS 5
#endif
#define MAX_LD_TASKS (MAX_TASKS - 1)


// Normally the stack grows from the end of the RAM range. Here we're allocating memory on the heap
// to use as independent stacks for our tasks. The "kernel" task will have an additional stack at
// the normal position in memory.
// We could also do this in a linker script.
#define STACK_SIZE 64
uint8_t stacks[MAX_LD_TASKS * STACK_SIZE];
#define TASK_PRGM_MEM_SIZE 2048
const uint8_t TASK_PGRM_MEM[TASK_PRGM_MEM_SIZE] PROGMEM = {0};


// Referenced in assembly code.
struct Task* current_task;

// For storing the "real" stack pointer. Referenced in assembly code.
uint8_t* kernel_sp;

// Used to track which task is active.
uint8_t task_idx = 0;

static struct Task tasks[MAX_LD_TASKS] = {0};

// Initialize the return pointer in the tasks' stacks.
void setup_start_funcs() {
	//for (uint8_t i = 0; i < NUM_TASKS; i++) {
		//// Add the function pointers to the stack. The stack grows down.
		//// Most things are little endian, but this address is stored big endian: https://www.avrfreaks.net/forum/big-endian-or-little-endian-0
		//*(tasks[i].stack_pointer) = (uint16_t)tasks[i].fun_ptr;
		//tasks[i].stack_pointer--;
		//*(tasks[i].stack_pointer) = (uint16_t)tasks[i].fun_ptr >> 8;
		//tasks[i].stack_pointer--;
		//// Space for preserved registers.
		//tasks[i].stack_pointer -= 18;
	//}
}


enum CmdTypes {
	CMD_LIST = 1,
	CMD_ENABLE = 2,
	CMD_WRITE = 3,
	CMD_DELETE = 4
};

void HandleListTasksCmd() {
	struct Task buffer;
	uint8_t* buffer_bytes = (uint8_t*)&buffer;
	buffer_bytes[0] = MAX_LD_TASKS;
	*((uint8_t const **)(buffer_bytes+1)) = TASK_PGRM_MEM;
	*((uint16_t *)(buffer_bytes+3)) = TASK_PRGM_MEM_SIZE;
	USART_Send(buffer_bytes, 5);
	for (int i = 0; i < MAX_LD_TASKS; i++) {
		buffer = tasks[i];
		uint8_t offset = 0;
		while (offset < sizeof(struct Task)) {
			offset += USART_Send(buffer_bytes, sizeof(struct Task) - offset);
		}
	}
}

void HandleWriteCmd() {
	uint8_t idx = 0;
	USART_Read(0, &idx, 1);
	uint16_t task_offset = 0;
	USART_Read(0, (uint8_t*)&task_offset, 2);
	uint16_t task_size = 0;
	USART_Read(0, &task_size, 2);
	
	uint8_t buffer;
	while (task_size > 0) {
		if (USART_Read(0, &buffer, 1) == 1) {
			
			task_offset++;
			task_size--;
		}
	}
}

void HandleDeleteCmd() {
	uint8_t idx = 0;
	USART_Read(0, &idx, 1);
	if (idx < MAX_LD_TASKS) {
		tasks[idx] = (const struct Task){ 0 };
	}
}

void HandleEnableCmd() {
	uint8_t idx = 0;
	USART_Read(0, &idx, 1);
	uint8_t is_enabled = 0;
	USART_Read(0, &is_enabled, 1);
	if (idx < MAX_LD_TASKS) {
		tasks[idx].enabled = is_enabled;
	}
}

void check_scheduler_cmds() {
	uint8_t cmd_type = 0;
	// This makes the big assumption that the serial input synced and stays synced.
	while (USART_Read(0, &cmd_type, 1)) {
		switch (cmd_type) {
			case CMD_LIST:
				HandleListTasksCmd();
				break;
			case CMD_ENABLE:
				HandleEnableCmd();
				break;
			case CMD_WRITE:
				HandleWriteCmd();
				break;
			case CMD_DELETE:
				HandleDeleteCmd();
				break;
		}
	}
}



int main(void)
{
	// PORTB pin 0/1 output
	DDRB = 0x3;
	setup_start_funcs();
	setup_scheduler_funcs();
	
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
		if (current_task->fun_ptr != 0 && current_task->enabled && is_time_past(current_task->next_run)) {
			// This switches to the stack for the current_task. Execution won't return here until that
			// task calls suspend_task.
			start_task();	
		}
		check_scheduler_cmds();
		task_idx = (task_idx + 1) % MAX_LD_TASKS;
	}
}
