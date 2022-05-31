/*
 * task3.c
 *
 * Created: 5/27/2022 3:34:58 AM
 * Author : feros
 */ 

#include <avr/io.h>

#ifndef F_CPU
	#define F_CPU 16000000
#endif

// We're tracking time based on timer1 which runs at F_CPU / 64.
// The casting to to avoid overflowing the integer sizes.
#define MS_TO_TICKS(ms) (F_CPU / (1000.0d * 64.0d / ((double)ms)))

typedef void (*delay_sig)(uint16_t);
#define DELAY_SYS_CALL ((delay_sig)0x1000)
// #define DELAY_SYS_CALL asm("call 0x1000")


int main(void)
{
	while (1) {
		PORTB |= 2;
		DELAY_SYS_CALL(MS_TO_TICKS(10));
		PORTB &= ~2;
		DELAY_SYS_CALL(MS_TO_TICKS(20));
	}
}
