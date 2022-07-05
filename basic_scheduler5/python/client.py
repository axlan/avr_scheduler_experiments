import sys
import struct

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
task_struct_format = '<HHHHH?'
task_struct_size = struct.calcsize(task_struct_format)


write_header_format = '<BBHH'

LIST_CMD = 1
ENABLE_CMD = 2
WRITE_CMD = 3
DELETE_CMD = 4

with Serial(port.name, baudrate=115200, timeout=1) as ser:
    # List command
    ser.write(b'\01')
    data = ser.read(list_header_size)
    print(data)
    (num_tasks, task_mem_offset, task_mem_size) = struct.unpack(list_header_format, data)
    print(f'num_tasks: {num_tasks}')
    print(f'task_mem_offset: 0x{task_mem_offset:X}')
    print(f'task_mem_size: {task_mem_size}')
    for i in range(num_tasks):
        data = ser.read(task_struct_size)
        task = struct.unpack(task_struct_format, data)
        print(task)

    DUMMY_DATA = "FATCAT" * 100

    data = struct.pack(write_header_format, WRITE_CMD, 0, task_mem_offset, len(DUMMY_DATA))

    ser.write(data)
    ser.write(DUMMY_DATA)
