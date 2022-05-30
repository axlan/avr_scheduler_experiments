import struct
import subprocess

tool_path = "C:/Program Files (x86)/Atmel/Studio/7.0/toolchain/avr8/avr8-gnu-toolchain/bin/"

project_dir = "C:/Users/feros/Documents/src/basic_scheduler/"

task_object_file = project_dir + "task3/Debug/main.o"

task_main_dump = project_dir + "basic_scheduler3/Debug/task3.bin"

scheduler_elf_file = project_dir + "basic_scheduler3/Debug/basic_scheduler3.elf"


subprocess.run([tool_path + 'avr-objcopy', "-O", "binary", "--only-section=.text.*", task_object_file, task_main_dump])

subprocess.run([tool_path + 'avr-objcopy', "--update-section", ".tasksection=" + task_main_dump, scheduler_elf_file])
