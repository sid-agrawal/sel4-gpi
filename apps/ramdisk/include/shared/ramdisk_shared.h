#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

/* Ramdisk configuration */
#define RAMDISK_DEBUG 0
#define RAMDISK_BLOCK_SIZE (1u << seL4_PageBits) // Block size for the ramdisk
#define RAMDISK_SIZE_BITS 21                                // Size of total ramdisk
#define RAMDISK_SIZE_BYTES (1u << RAMDISK_SIZE_BITS)

/* Memory regions for IPC to ramdisk server */
#define RAMDISK_MR_OP 0
#define RAMDISK_CAP_MO 0

/* Ramdisk opcodes */
enum RAMDISK_OPCODE
{
    RAMDISK_READ,
    RAMDISK_WRITE,
    RAMDISK_GET_BLOCK,
    RAMDISK_SANITY_TEST
};