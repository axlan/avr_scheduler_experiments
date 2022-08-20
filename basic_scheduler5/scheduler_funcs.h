#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <stdbool.h>

// This is the set of "SystemCalls" tasks will have access to.
// The functions are defined in the main scheduler build.
struct SchedulerFuncs {
	void (*delay_ms)(uint16_t);
	void (*get_lock)(void);
	bool (*is_lock_available)(void);
	void (*release_lock)(void);
	uint8_t (*usart_write)(const void*, uint8_t );
	uint8_t (*usart_write_free)();
	uint8_t (*usart_read)(void*, uint8_t);
	const char* (*get_task_name)(uint8_t*);
};

// .scheduler_funcs needs to be set to the same value in the scheduler build, and the linking of each task.
__attribute__((__section__(".scheduler_funcs")))
struct SchedulerFuncs scheduler;

// For simplicity tasks need to have their entry point at the start of the .text section.
// This is normally where the vector table is, so using this attribute on the entry point function to put it there instead.
#define TASK_ENTRY __attribute__((section(".vectors")))

#endif /* SCHEDULER_H_ */