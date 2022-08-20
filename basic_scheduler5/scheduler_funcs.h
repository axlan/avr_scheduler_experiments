/*
 * scheduler.h
 *
 * Created: 6/12/2022 3:23:33 PM
 *  Author: feros
 */ 


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

__attribute__((__section__(".scheduler_funcs")))
struct SchedulerFuncs scheduler;

#define TASK_ENTRY __attribute__((section(".vectors")))

#endif /* SCHEDULER_H_ */