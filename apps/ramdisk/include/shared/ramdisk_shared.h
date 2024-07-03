#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>

/* Ramdisk configuration */
#define RAMDISK_DEBUG 0
#define BLOCK_RESOURCE_TYPE_NAME "BLOCK"
#define RAMDISK_BLOCK_SIZE (1u << seL4_PageBits) // Block size for the ramdisk
#define RAMDISK_SIZE_BITS 22                     // Size of total ramdisk
#define RAMDISK_SIZE_BYTES (1u << RAMDISK_SIZE_BITS)