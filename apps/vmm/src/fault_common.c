#include <sel4/sel4.h>
#include <vmm-common/fault_common.h>

char *fault_to_string(seL4_Word fault_label)
{
    switch (fault_label)
    {
    case seL4_Fault_VMFault:
        return "virtual memory";
    case seL4_Fault_UnknownSyscall:
        return "unknown syscall";
    case seL4_Fault_UserException:
        return "user exception";
    case seL4_Fault_VGICMaintenance:
        return "VGIC maintenance";
    case seL4_Fault_VCPUFault:
        return "VCPU";
    case seL4_Fault_VPPIEvent:
        return "VPPI event";
    default:
        return "unknown fault";
    }
}
