#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

/* Ramdisk configuration */
#define RAMDISK_DEBUG 0
#define RAMDISK_BLOCK_SIZE (1u << seL4_PageBits) // Block size for the ramdisk
#define RAMDISK_SIZE_BITS 21                     // Size of total ramdisk
#define RAMDISK_SIZE_BYTES (1u << RAMDISK_SIZE_BITS)

/* API of the ramdisk server */

/* IPC values returned in the "label" message header. */
enum rd_errors
{
    RD_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    RD_SERVER_ERROR_UNKNOWN = seL4_NumErrors,
    RD_SERVER_ERROR_NO_BLOCKS,
};

/* IPC Message register values for FSMSGREG_FUNC */
enum rd_server_funcs
{
    RD_FUNC_CREATE_REQ = 0,
    RD_FUNC_CREATE_ACK,

    RD_FUNC_READ_REQ,
    RD_FUNC_READ_ACK,

    RD_FUNC_WRITE_REQ,
    RD_FUNC_WRITE_ACK,

    RD_FUNC_SANITY_REQ,
    RD_FUNC_SANITY_ACK,
};

enum rd_msgregs
{
    /* These are fixed headers in every rd message. */
    RDMSGREG_FUNC = 0,

    // Ramdisk server API calls have no arguments
};