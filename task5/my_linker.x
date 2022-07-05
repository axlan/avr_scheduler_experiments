SECTIONS
{
  .mySegment 0x00000068 : {KEEP(*(.scheduler_funcs))}
  .mySegment2 TASK_OFFSET : {KEEP(*(.task))}
}
