#pragma once
#include <sel4/sel4.h>

/* Take the fault label given by the kernel and convert it to a string. */
char *fault_to_string(seL4_Word fault_label);
