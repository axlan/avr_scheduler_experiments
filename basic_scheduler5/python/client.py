from argparse import ArgumentParser
from ast import arg
from faulthandler import is_enabled
import sys
import os
import math
import struct
import subprocess
import threading
from attr import fields

from colorama import init, Fore, Back, Style
from serial import Serial
from serial.tools import list_ports

# // This is what's needed to specify a task.
# struct Task {
# 	// All the state is on the stack with it's end at this pointer.
# 	uint8_t* stack_pointer;
# 	// This is used to point to the function for the task to start at.
# 	uint16_t task_offset;
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

enable_header_format = '<BBB'

del_header_format = '<BB'

LIST_CMD = 1
ENABLE_CMD = 2
WRITE_CMD = 3
DELETE_CMD = 4

PAGE_SIZE = 128

tool_path = 'C:/Program Files (x86)/Atmel/Studio/7.0/toolchain/avr8/avr8-gnu-toolchain/bin/'

project_path = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..'))
elf_out = project_path + '/basic_scheduler5/python/out/task.elf'
task_dump = project_path + '/basic_scheduler5/python/out/task.bin'


def compile_task(object_file, start_offset):
    section_addr = f'0x{start_offset:X}'

    ret = subprocess.call([tool_path + "avr-gcc.exe", "-o", elf_out, object_file, '-nostartfiles', '-Wl,-static',
                           f'-Wl,-section-start=.text={section_addr}', f'-Wl,-section-start=.scheduler_funcs=0x800310', '-mmcu=atmega168', '-B',
                           'C:\Program Files (x86)\Atmel\Studio\7.0\Packs\atmel\ATmega_DFP\1.6.364\gcc\dev\atmega168'])
    if ret:
        exit(1)


    proc = subprocess.run([tool_path + 'avr-objdump', "-h", "-S", elf_out], stdout=subprocess.PIPE)
    output = proc.stdout.decode('utf-8').replace('\r', '')

    # Generate dump of loaded task
    with open(f'{elf_out}.{start_offset}.lss', "w") as outfile:
        outfile.write(output)

    # Check for .data usage
    for line in output.splitlines():
        if "Disassembly" in line:
            break
        fields = line.split()
        if len(fields) == 7:
            if fields[1] == ".scheduler_funcs":
                continue
            if fields[3].startswith('0080') and int(fields[2],16) != 0:
                print("Tasks can't use the RAM directly. All data must be on the stack.")
                exit(1) 

    ret = subprocess.call([tool_path + 'avr-objcopy', "-O",
                          "binary", "--only-section=.text", elf_out, task_dump])
    if ret:
        exit(1)


def get_loaded_tasks(task_state):
    return [task for task in task_state['tasks'] if task['size'] > 0]


def get_sorted_tasks_with_pages(task_state):
    loaded_tasks = get_loaded_tasks(task_state)
    loaded_tasks = sorted(loaded_tasks, key=lambda x: x['offset'])
    for task in loaded_tasks:
        task['page_offset'] = int(math.floor(
            (task['offset'] - task_state['task_mem_offset'])/float(PAGE_SIZE)))
        task['num_pages'] = int(math.ceil(task['size']/float(PAGE_SIZE)))
    return loaded_tasks


def load_task(ser, object_file, task_name, task_state):
    found_task = None
    # Check for free task
    for task in task_state['tasks']:
        if task['size'] == 0:
            found_task = task
            break

    if found_task is None:
        print('No task slots available. Delete a task first.')
        exit(1)

    # Get the binary size
    compile_task(object_file, 0x100)
    file_size = os.path.getsize(task_dump)
    pages_needed = int(math.ceil(file_size/float(PAGE_SIZE)))

    TOTAL_PAGES = int(task_state['task_mem_size'] / PAGE_SIZE)
    loaded_tasks = get_sorted_tasks_with_pages(task_state)

    start_page = None

    if len(loaded_tasks) == 0:
        start_page = 0
    else:
        for i in range(len(loaded_tasks) - 1):
            end_offset = loaded_tasks[i]['page_offset'] + \
                loaded_tasks[i]['num_pages']
            space = loaded_tasks[i+1]['page_offset'] - end_offset
            if space >= pages_needed:
                start_page = end_offset
                break

        if start_page is None:
            end_offset = loaded_tasks[-1]['page_offset'] + \
                loaded_tasks[-1]['num_pages']
            space = TOTAL_PAGES - end_offset
            if space >= pages_needed:
                start_page = end_offset
            else:
                print('Not enough memory available. Delete a task first.')
                exit()

    start_offset = start_page * PAGE_SIZE + task_state['task_mem_offset']
    compile_task(object_file, start_offset)
    with open(task_dump, 'rb') as fd:
        task_data = fd.read()

    # Write Cmd

    data = struct.pack(write_header_format, WRITE_CMD, found_task['index'], start_offset, len(
        task_data), task_name.encode('ascii'))
    ser.write(data)
    i = 0
    while i < len(task_data):
        data = ser.read(2)
        print(struct.unpack('<H', data)[0])
        ser.write(task_data[i:i+PAGE_SIZE])
        i += PAGE_SIZE


def del_task(ser, idx):
    data = struct.pack(del_header_format, DELETE_CMD, idx)
    ser.write(data)


def get_task_list(ser):
    ser.write(bytes([LIST_CMD]))
    data = ser.read(list_header_size)
    (num_tasks, task_mem_offset, task_mem_size) = struct.unpack(
        list_header_format, data)
    task_state = {
        'num_tasks': num_tasks,
        'task_mem_offset': task_mem_offset,
        'task_mem_size': task_mem_size,
        'tasks': []
    }
    for i in range(num_tasks):
        data = ser.read(task_struct_size)
        task = struct.unpack(task_struct_format, data)
        task_state['tasks'].append({
            'stack_ptr': task[0],
            'offset': task[1],
            'next_run': task[2],
            'name': task[3].decode("ascii").replace('\x00', ''),
            'size': task[4],
            'enabled': task[5],
            'index': i,
        })
    return task_state


def enable_task(ser, idx, is_enabled):
    enable_val = 1 if is_enabled else 0
    data = struct.pack(enable_header_format, ENABLE_CMD, idx, enable_val)
    ser.write(data)


def reset_style():
    print(Style.RESET_ALL, end='')


def draw_tasks(task_state):
    TOTAL_PAGES = int(task_state['task_mem_size'] / PAGE_SIZE)
    loaded_tasks = get_sorted_tasks_with_pages(task_state)
    for task in task_state['tasks']:
        if task['size'] > 0:
            print(f'Task {task["index"]}: ', end='')
            if task['enabled']:
                color = Fore.GREEN
            else:
                color = Fore.RED
            print(color + task["name"])
            reset_style()
        else:
            print(f'Task {task["index"]} not loaded')

    print('|', end='')
    for i in range(TOTAL_PAGES):
        found = False
        for task in loaded_tasks:
            if i >= task['page_offset'] and i < task['page_offset'] + task['num_pages']:
                if task['enabled']:
                    print(Back.GREEN + str(task['index']), end='')
                else:
                    print(Back.RED + str(task['index']), end='')
                found = True
        if not found:
            print('-', end='')
        reset_style()
    print('|')


def get_task(task_id: str, task_state):
    if task_id.isdecimal():
        i = int(task_id)
        if i < len(task_state['tasks']):
            return i
    else:
        for i, task in enumerate(task_state['tasks']):
            if task['name'] == task_id:
                return i

    return -1


def term_input_func(ser):
    try:
        while True:
            c = ser.read().decode('ascii')
            print(Fore.CYAN + c, end='')
            reset_style()
    except:
      pass


def main():
    init()

    parser = ArgumentParser(
        usage='python client.py COMMAND [OPTIONS]...',
        description='Update device lever arm configurations.',
        epilog="""\
EXAMPLE USAGE
""")

    parser.add_argument('--device-port', default="auto",
                        help="The serial device to use when communicating with the device.  If 'auto', the serial port "
                             "will be located automatically by searching for a connected device.")

    command_subparsers = parser.add_subparsers(
        dest='command',
        help='The command to be run.')

    list_parser = command_subparsers.add_parser(
        'list',
        help='List the current device status.')

    term_parser = command_subparsers.add_parser(
        'term',
        help='Open a terminal to the device.')

    enable_parser = command_subparsers.add_parser(
        'enable',
        help='Enabled/disable a task by name or index.')
    enable_parser.add_argument('task', help='The name or id of the task.')
    enable_parser.add_argument(
        'is_enabled', nargs='?', default='true', help='Whether to enabled (true/1) or disable the task.')

    load_parser = command_subparsers.add_parser(
        'load',
        help='Load a task into chip flash.')
    load_parser.add_argument('name', help='The name for the task.')
    load_parser.add_argument(
        'object_file', help='The compiled object file for the task.')

    del_parser = command_subparsers.add_parser(
        'del',
        help='Delete a task by name or index.')
    del_parser.add_argument('task', help='The name or id of the task.')

    args = parser.parse_args()

    if args.device_port == 'auto':
        ports = list_ports.comports()
        port_name = ports[0].name
    else:
        port_name = args.device_port

    with Serial(port_name, baudrate=115200, timeout=1) as ser:
        # Flush the read buffer.
        ser.read_all()
        if args.command is None:
            print('No command specified.\n')
            parser.print_help()
            sys.exit(0)

        task_state = get_task_list(ser)
        if hasattr(args, 'task'):
            idx = get_task(args.task, task_state)
            if idx == -1:
                print(f'Task {args.task} not found.')
                exit(1)

        if args.command == 'list':
            draw_tasks(task_state)
        elif args.command == 'enable':
            if task_state['tasks'][idx]['size'] == 0:
                print(f"Can't enable task {idx} since no task is loaded.")
            is_enabled = args.is_enabled == "1" or args.is_enabled.lower() == 'true'
            enable_task(ser, idx, is_enabled)
        elif args.command == 'load':
            if len(args.name) > 15:
                print(f"{args.name} too long. Max length 15 characters.")
                exit(1)
            load_task(ser, args.object_file, args.name, task_state)
        elif args.command == 'del':
            if task_state['tasks'][idx]['size'] == 0:
                print(f'Task {args.task} already deleted.')
                exit(0)
            del_task(ser, idx)
        elif args.command == 'term':
            threading.Thread(target=term_input_func,
                             args=(ser,), daemon=True).start()
            ser.timeout = None
            try:
                while 1:
                    txt = input()
                    ser.write(txt.encode('ascii'))
            except:
                exit(0)


if __name__ == '__main__':
    main()

# python basic_scheduler5/python/client.py list
# python basic_scheduler5/python/client.py load Task1 task5/Debug/library.o
# python basic_scheduler5/python/client.py enable Task1

