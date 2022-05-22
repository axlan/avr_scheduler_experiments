; Formatting based on https://ucexperiment.wordpress.com/2012/02/09/mixing-c-and-assembly-in-avr-gcc-and-avr-studio-4/
#include <avr/io.h>

; Make these visible to the C code.
.global start_task
.global suspend_task

; Switch the stack from the main kernel stack to the custom stack pointed to by current_task
start_task:
	; TBD save R2–R17, R28, R29
	; Save the stack pointer register to kernel_sp
	; Using the direct SRAM instructions since we're writing to a fixed address
	lds r18,SPL
	sts kernel_sp, r18
	lds r18,SPH
	sts kernel_sp+1, r18
	; Set the stack pointer for the task
	; need an additional level of indirection
	; Setting up X register (r26:r27)
	; The pointer current_task points to the stack pointer of currently active task.
	lds r26,current_task
	lds r27,current_task+1
	; Use X register to copy the stack pointer, to the stack pointer register.
	ld r18,X+
	sts SPL,r18
	ld r18,X
	sts SPH,r18
	; For the first call the stack pointer points to the start function set by setup_start_funcs.
	; For later calls, this points to the suspend_task caller.
	ret

; This is basically the reverse of start_task to return to the kernel exectution
suspend_task:
	; Save the task stack pointer
	lds r26,current_task
	lds r27,current_task+1
	lds r18,SPL
	st X+, r18
	lds r18,SPH
	st X, r18
	; Restore the main task SP
	lds r18,kernel_sp
	sts SPL, r18
	lds r18,kernel_sp+1
	sts SPH, r18
	; return to the main task
	ret
