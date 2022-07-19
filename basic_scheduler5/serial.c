/*
 * IncFile1.h
 *
 * Created: 6/5/2022 8:44:56 AM
 *  Author: feros
 */ 
#include "serial.h"
#include <avr/boot.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#ifndef F_CPU
	#define F_CPU 16000000
#endif
// This controls how many independent threads can have their reads tracked.
#ifndef MAX_TASKS
	#define MAX_TASKS 5
#endif
// The size of the shared transmit buffer.
#ifndef TX_BUFFER_LEN
	#define TX_BUFFER_LEN 128
#endif
// The max depth of the receive queue for the slowest task.
#ifndef RX_BUFFER_LEN
	#define RX_BUFFER_LEN 16
#endif

/** 
 * Increment a circular buffer pointer with rollover.
 */
static inline uint8_t IncrementWithRollover(uint8_t val, uint8_t capacity) {
	return (val + 1) % capacity;
}

/** 
 * Push a value into a circular buffer and update the buffer head pointer.
 * If the buffer is already full, data won't be inserted and false is returned.
 */
static inline bool RingBufferPush (uint8_t data, uint8_t* head, uint8_t tail, uint8_t* buffer, uint8_t capacity) {
	uint8_t next = IncrementWithRollover(*head, capacity);
	// The ring has wrapped and the next value would overwrite the tail.
	if (next == tail) {
		return false;
	}
	buffer[*head] = data;
	*head = next;
	return true;
}

/** 
 * Read a value off a circular buffer and update the buffer tail pointer.
 * If the buffer is empty, data won't be read and false is returned.
 */
static inline bool RingBufferPop (uint8_t* data, uint8_t head, uint8_t *tail, const uint8_t* buffer, uint8_t capacity) {
	// No data to pop.
	if (head == *tail) {
		return false;
	}
	*data = buffer[*tail];
	*tail = IncrementWithRollover(*tail, capacity);
	return true;
}

// This buffer should be interrupt safe since the IRQ and main execution don't touch the same variables and since the values are read atomically.
// The USART_Send updates the head and buffer value, and the IRQ updates the tail.
static uint8_t serial_tx_head = 0;
static volatile uint8_t serial_tx_tail = 0;
static volatile uint8_t serial_tx_buffer[TX_BUFFER_LEN];

// This buffer should be interrupt safe since the IRQ and main execution don't touch the same variables and since the values are read atomically.
// The IRQ updates the head, error, and buffer values. USART_read updates the tail.
// The reads are tracked independently for each task, but they share a single buffer.
static volatile uint8_t serial_rx_head = 0;
static uint8_t serial_rx_tail[MAX_TASKS] = {0};
// Could be bit mask
static volatile bool serial_rx_error[MAX_TASKS] = {0};
static volatile uint8_t serial_rx_buffer[RX_BUFFER_LEN];


void USART_Init (uint32_t baud)
{
	/* Set baud rate */
	// UBRR = F_OSC/(8 * baud) - 1
	// For 16MHz and 115200 baud = 16.36. 16 gives rate 117647.
	// Add 0.5 to apply rounding.
	// Much more efficient to do this without floating point eventually.
	uint16_t ubbr_calc = ((double)F_CPU) / (8.0 * (double)baud) - 1.0 + 0.5;
	//uint16_t ubbr_calc = 16;
	UBRR0L = ubbr_calc;
	/* Enable double rate clock gen */
	UCSR0A = (1<<U2X0);
	//Enable receiver and transmitter and Rx interrupt.
	//Since the Tx ready interrupt triggers continuously if nothing is being sent, it will only be enabled during writes.
	UCSR0B = (1<<RXCIE0)|(1<<RXEN0)|(1<<TXEN0);
	/* Set frame format: 8data*/
	UCSR0C = (3<<UCSZ00);
}

uint8_t USART_Send(const void* data, uint8_t len) {
	uint8_t ret = 0;
	// Add data unless the buffer is full.
	for (uint8_t i = 0; i < len; i++) {
		if (!RingBufferPush(((uint8_t*)data)[i], &serial_tx_head, serial_tx_tail, (uint8_t*)serial_tx_buffer, TX_BUFFER_LEN)) {
			break;
		}
		ret++;
	}
	/* Enable interrupt to push out data when ready. */
	UCSR0B |= 1<<UDRIE0;
	return ret;
}

uint8_t USART_Read(uint8_t task_idx, void* data, uint8_t len) {
	uint8_t ret = 0;
	// Read off available data that fits in the output buffer.
	for (uint8_t i = 0; i < len; i++) {
		if(!RingBufferPop(((uint8_t*)data) + i, serial_rx_head, serial_rx_tail + task_idx, (uint8_t*)serial_rx_buffer, RX_BUFFER_LEN)) {
			break;
		}
		ret++;
	}
	return ret;
}

bool Check_New_Error(uint8_t task_idx) {
	if (serial_rx_error[task_idx]) {
		serial_rx_error[task_idx] = false;
		return true;
	}
	return false;
}

void USART_Rx_Clear(uint8_t task_idx) {
	serial_rx_tail[task_idx] = serial_rx_head;
	serial_rx_error[task_idx] = false;
}

uint8_t USART_Rx_Bytes_Buffered(uint8_t task_idx) {
	// Avoid race condition where head is updated during call.
	uint8_t local_head = serial_rx_head;
	if (serial_rx_tail[task_idx] <= local_head) {
		return local_head - serial_rx_tail[task_idx];
	}
	// If head rolled over and tail hasn't
	else {
		return local_head + RX_BUFFER_LEN - serial_rx_tail[task_idx];
	}
}

// Data Tx register empty interrupt.
ISR(USART_UDRE_vect)
{
	if(!RingBufferPop((uint8_t*)&UDR0, serial_tx_head, (uint8_t*)&serial_tx_tail, (uint8_t*)serial_tx_buffer, TX_BUFFER_LEN)) {
		/* Disable interrupt if no more data. */
		UCSR0B &= ~(1<<UDRIE0);
	}
}

// UART received byte interrupt.
ISR(USART_RX_vect)
{
	uint8_t next = IncrementWithRollover(serial_rx_head, RX_BUFFER_LEN);
	// Check if this push is going to overflow the buffer for any of the tasks.
	for(uint8_t i = 0; i < MAX_TASKS; i++) {
		serial_rx_error[i] = serial_rx_tail[i] == next;
	}
	// Use serial_rx_head for tail since we always want this to go through.
	RingBufferPush(UDR0, (uint8_t*)&serial_rx_head, serial_rx_head, (uint8_t*)serial_rx_buffer, RX_BUFFER_LEN);
}
