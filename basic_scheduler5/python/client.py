import sys
import math
import struct
import subprocess

from serial import Serial
from serial.tools import list_ports

ports = list_ports.comports()

port = ports[0]

# // This is what's needed to specify a task.
# struct Task {
# 	// All the state is on the stack with it's end at this pointer.
# 	uint8_t* stack_pointer;
# 	// This is used to point to the function for the task to start at.
# 	void (*fun_ptr)(void);
# 	// This is used to schedule when the task will run next.
# 	uint16_t next_run;
# 	char name[16];
# 	uint16_t size;
# 	bool enabled; 
# };
list_header_format = '<BHH'
list_header_size = struct.calcsize(list_header_format)
task_struct_format = '<HHH16sH?'
task_struct_size = struct.calcsize(task_struct_format)

write_header_format = '<BBHH16s'

enable__header_format = '<BBB'

LIST_CMD = 1
ENABLE_CMD = 2
WRITE_CMD = 3
DELETE_CMD = 4

PAGE_SIZE = 128

tool_path = 'C:/Program Files (x86)/Atmel/Studio/7.0/toolchain/avr8/avr8-gnu-toolchain/bin/'

project_path = 'C:/Users/feros/Documents/src/basic_scheduler/'

object_file = project_path + 'task5/Debug/library.o'

elf_out = project_path + 'basic_scheduler5/python/out/task.elf'
task_dump = project_path + 'basic_scheduler5/python/out/task.bin'

with Serial(port.name, baudrate=115200, timeout=1) as ser:
    # List command
    ser.write(b'\01')
    data = ser.read(list_header_size)
    (num_tasks, task_mem_offset, task_mem_size) = struct.unpack(list_header_format, data)
    print(f'num_tasks: {num_tasks}')
    print(f'task_mem_offset: 0x{task_mem_offset:X}')
    print(f'task_mem_size: {task_mem_size}')
    for i in range(num_tasks):
        data = ser.read(task_struct_size)
        task = struct.unpack(task_struct_format, data)
        print(task)

    start_offset = int(math.ceil(task_mem_offset / PAGE_SIZE)) * PAGE_SIZE
    section_addr = f'0x{start_offset:X}'

    ret = subprocess.call([tool_path + "avr-gcc.exe", "-o", elf_out, object_file, '-nostartfiles', '-Wl,-static',
                        f'-Wl,-section-start=.text={section_addr}', f'-Wl,-section-start=.scheduler_funcs=0x800310', '-mmcu=atmega168', '-B',
                        'C:\Program Files (x86)\Atmel\Studio\7.0\Packs\atmel\ATmega_DFP\1.6.364\gcc\dev\atmega168'])
    if ret:
        exit(1)

    ret = subprocess.call([tool_path + 'avr-objcopy', "-O", "binary", "--only-section=.text", elf_out, task_dump])
    if ret:
        exit(1)

    with open(task_dump, 'rb') as fd:
        task_data = fd.read()

    # Write Cmd

    data = struct.pack(write_header_format, WRITE_CMD, 0, start_offset, len(task_data), b"CatTask")
    ser.write(data)
    i = 0
    while i < len(task_data):
       data = ser.read(2)
       print(struct.unpack('<H', data)[0])
       ser.write(task_data[i:i+PAGE_SIZE])
       i += PAGE_SIZE

    # Enable Cmd
    data = struct.pack(enable__header_format, ENABLE_CMD, 0, 1)
    ser.write(data)
