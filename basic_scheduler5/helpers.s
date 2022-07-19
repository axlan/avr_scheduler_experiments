; Formatting based on https://ucexperiment.wordpress.com/2012/02/09/mixing-c-and-assembly-in-avr-gcc-and-avr-studio-4/
#include <avr/io.h>

; Make these visible to the C code.
.global start_task
.global suspend_task

; Switch the stack from the main kernel stack to the custom stack pointed to by current_task
start_task:
    ; Store the preserved registers for kernel
	push R2
	push R3
	push R4
	push R5
	push R6
	push R7
	push R8
	push R9
	push R10
	push R11
	push R12
	push R13
	push R14
	push R15
	push R16
	push R17
	push R28
	push R29
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
	; Restore the preserved registers for task
	pop R29
	pop R28
	pop R17
	pop R16
	pop R15
	pop R14
	pop R13
	pop R12
	pop R11
	pop R10
	pop R9
	pop R8
	pop R7
	pop R6
	pop R5
	pop R4
	pop R3
	pop R2
	; For the first call the stack pointer points to the start function set by setup_start_funcs.
	; For later calls, this points to the suspend_task caller.
	ret

; This is basically the reverse of start_task to return to the kernel exectution
suspend_task:
    ; Store the preserved registers for task
	push R2
	push R3
	push R4
	push R5
	push R6
	push R7
	push R8
	push R9
	push R10
	push R11
	push R12
	push R13
	push R14
	push R15
	push R16
	push R17
	push R28
	push R29
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
	; Restore the preserved registers for kernel
	pop R29
	pop R28
	pop R17
	pop R16
	pop R15
	pop R14
	pop R13
	pop R12
	pop R11
	pop R10
	pop R9
	pop R8
	pop R7
	pop R6
	pop R5
	pop R4
	pop R3
	pop R2
	; return to the main task
	ret
