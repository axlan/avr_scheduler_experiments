/*
 * IncFile1.h
 *
 * Created: 6/5/2022 8:44:56 AM
 *  Author: feros
 */ 

#ifndef INCFILE1_H_
#define INCFILE1_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize the UART registers for the given baud rate. 
 */
void USART_Init (uint32_t baud);

/**
 * Send as much of the data as possible without blocking.
 * Returns the number of bytes sent.
 */
uint8_t USART_Send(const uint8_t* data, uint8_t len);

/**
 * Read as much of the data as possible without blocking.
 * Each task_idx allows for separate tracking of which bytes have been read.
 * Returns the number of bytes read.
 */
uint8_t USART_Read(uint8_t task_idx, uint8_t* data, uint8_t len);

/**
 * Clear the read buffer for one of the tasks.
 */
void USART_Rx_Clear(uint8_t task_idx);

/**
 * Check if a task isn't reading fast enough and dropped data.
 * This clears the error if it was triggered. 
 */
bool Check_New_Error(uint8_t task_idx);

#endif /* INCFILE1_H_ */