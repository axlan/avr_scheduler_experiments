/*
 * Here I add some basic logic to include a timer for the scheduler to let tasks sleep for
 * specified amount of time.
 * Comments assume F_CPU == 16e6 (default Arduino clock)
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/boot.h>
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

// TODO: Need to persistently load/store the size/offset to EEPROM to persist the tasks between power cycles.
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



#define READ_UART_BYTE(data) \
  while (!(UCSR0A & (1<<RXC0))); \
  data = UDR0;
  
#define WRITE_UART_BYTE(data) \
  while (!(UCSR0A & (1<<UDRE0))); \
  UDR0 = data;
  
enum SpmState {
	SPM_STATE_WRITING,
	SPM_STATE_ERASING,
	SPM_READY
};


void BOOTLOADER_SECTION HandleWriteCmd() {
	// Block until header is received.
	while (USART_Rx_Bytes_Buffered(0) < 5);
	
	// Since the UART interrupts are in the RWW memory disable them. Ideally, I'd move them to the NRWW section,
	// but I don't want to do the config to move the vector table.
	cli();
	
	uint8_t idx = 0;
	USART_Read(0, &idx, 1);
	uint16_t task_offset = 0;
	USART_Read(0, (uint8_t*)&task_offset, 2);
	uint16_t task_size = 0;
	USART_Read(0, &task_size, 2);
	
	
	// This should probably be done at the end of this process, but doing it here to reduce the amount of book keeping
	tasks[idx].fun_ptr = (task_sig)task_offset;
	tasks[idx].size = task_size;
	
	// I found the instructions in the datasheet a little unclear about when writing to the flash buffer is allowed.
	// My understanding is that you can write at any time, but doing the flash write clears it.
	
	// At 115200 baud 128 bytes transfer in about 11ms. The flash erase and write supposed
	// to be about 4ms each. Ideally I should be able to receive and write the bytes to
	// the temporary buffer while these operations are going.
	

	
	// I'm assuming the start offset falls on a page boundary and is a multiple of 2 bytes large.
	while (task_size > 0) {

/*
		uint8_t word[2];
		boot_spm_busy_wait();
		boot_page_erase (task_offset);
		boot_spm_busy_wait();
		
		// Send the offset to sync the writes from the host
		WRITE_UART_BYTE(task_offset & 0xFF);
		WRITE_UART_BYTE(task_offset >> 8);
		
		for (int i = 0; i<SPM_PAGESIZE && task_size > 0; i +=2 ) {
			READ_UART_BYTE(word[0]);
			READ_UART_BYTE(word[1]);
			task_size-=2;
			boot_page_fill (task_offset + i, *(uint16_t*)word);
		}
		
		boot_page_write (task_offset);
		task_offset += SPM_PAGESIZE;
*/
/*
		// Send the offset to sync the writes from the host
		WRITE_UART_BYTE(task_offset & 0xFF);
		WRITE_UART_BYTE(task_offset >> 8);
		// This loop is trying to do a few things in parallel:
		// * Wait for the previous write to finish and erase the next page when ready.
		//   The first time this is called no write is in progress so this should occur immediately.
		// * Receive bytes from the UART to write.
		// * Write the received bytes to the temporary buffer.
		int i = 0;
		uint8_t word[2];
		enum SpmState state = SPM_STATE_WRITING;
		while (true) {
			// Check if the Flash is free to erase.
			if (state == SPM_STATE_WRITING && !boot_spm_busy()) {
				boot_page_erase (task_offset);
				state = SPM_STATE_ERASING;
			// The erase finished.
			} else if (state == SPM_STATE_ERASING && !boot_spm_busy()) {
				state = SPM_READY;
			}
			
			if (i<SPM_PAGESIZE && task_size > 0) {
				// Check if a new UART byte was received.
				// If it's the second byte in the word, write it to the temp buffer.
				if (UCSR0A & (1<<RXC0)) {
					word[i % 2] = UDR0;
					i++;
					task_size--;
					if (i % 2 == 0) {
						boot_page_fill (task_offset + i - 2, *(uint16_t*)word);	
					}
				}
			} else if (state == SPM_READY) {
				break;
			}
		}
		// With the full page in the temporary buffer, write it to flash.
		boot_page_write (task_offset);
		// This doesn't help.
		// boot_spm_busy_wait(); 
		task_offset += i;
*/

		// Send the offset to sync the writes from the host
		WRITE_UART_BYTE(task_offset & 0xFF);
		WRITE_UART_BYTE(task_offset >> 8);
		// This loop is trying to do a few things in parallel:
		// * Wait for the previous write to finish and erase the next page when ready.
		//   The first time this is called no write is in progress so this should occur immediately.
		// * Receive bytes from the UART to write.
		// * Write the received bytes to the temporary buffer.
		int i = 0;
		enum SpmState state = SPM_STATE_WRITING;
		while (true) {
			// Check if the Flash is free to erase.
			if (state == SPM_STATE_WRITING && !boot_spm_busy()) {
				boot_page_erase (task_offset);
				state = SPM_STATE_ERASING;
			// The erase finished.
			} else if (state == SPM_STATE_ERASING && !boot_spm_busy()) {
				state = SPM_READY;
			}
			
			if (i<SPM_PAGESIZE && task_size > 0) {
				// Check if a new UART byte was received.
				// If it's the second byte in the word, write it to the temp buffer.
				if (UCSR0A & (1<<RXC0)) {
					stacks[i] = UDR0;
					i++;
					task_size--;
				}
			} else if (state == SPM_READY) {
				break;
			}
		}

		for (i = 0; i<SPM_PAGESIZE; i +=2 ) {
			boot_page_fill (task_offset + i, *(uint16_t*)(stacks + i));
		}
		
		// With the full page in the temporary buffer, write it to flash.
		boot_page_write (task_offset);
		task_offset += SPM_PAGESIZE;

	}
	
	boot_spm_busy_wait();       // Wait until the memory is written.

	// Reenable RWW-section again. We need this if we want to jump back
	// to the application after bootloading.

	boot_rww_enable ();
	
	sei();
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