#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

/* ramdisk configuration */
#define RAMDISK_BLOCK_SIZE SIZE_BITS_TO_BYTES(seL4_PageBits) // Block size for the ramdisk
#define RAMDISK_SIZE_BITS 17                                // Size of total ramdisk
#define RAMDISK_SIZE_BYTES SIZE_BITS_TO_BYTES(RAMDISK_SIZE_BITS)

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