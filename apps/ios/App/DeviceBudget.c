#include <mach/mach.h>
#include <os/proc.h>
#include <stdint.h>

uint64_t sc_process_footprint(void) {
    task_vm_info_data_t memory = {0};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&memory, &count) != KERN_SUCCESS) return 0;
    return memory.phys_footprint;
}

uint64_t sc_available_memory(void) { return os_proc_available_memory(); }

