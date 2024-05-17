#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_remote_utils.h>

/* Ramdisk configuration */
#define RAMDISK_DEBUG 0
#define RAMDISK_BLOCK_SIZE (1u << seL4_PageBits) // Block size for the ramdisk
#define RAMDISK_SIZE_BITS 22                     // Size of total ramdisk
#define RAMDISK_SIZE_BYTES (1u << RAMDISK_SIZE_BITS)

/* API of the ramdisk server */

/* IPC values returned in the "label" message header. */
enum rd_errors
{
    RD_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    RD_SERVER_ERROR_UNKNOWN = RS_NUM_ERRORS,
    RD_SERVER_ERROR_NO_BLOCKS,
};

/* IPC Message register values for RDMSGREG_FUNC */
enum rd_server_funcs
{
    RD_FUNC_CREATE_REQ = RS_FUNC_END,
    RD_FUNC_CREATE_ACK,

    RD_FUNC_READ_REQ,
    RD_FUNC_READ_ACK,

    RD_FUNC_WRITE_REQ,
    RD_FUNC_WRITE_ACK,

    RD_FUNC_BIND_REQ,
    RD_FUNC_BIND_ACK,

    RD_FUNC_UNBIND_REQ,
    RD_FUNC_UNBIND_ACK,
};

enum rd_msgregs
{
    /* These are fixed headers in every rd message. */
    RDMSGREG_FUNC = RSMSGREG_FUNC,

    /* This is a convenience label for IPC MessageInfo length. */
    RDMSGREG_LABEL0,

    /* Create */
    RDMSGREG_CREATE_REQ_END = RDMSGREG_LABEL0,

    RDMSGREG_CREATE_ACK_SPACE_ID = RDMSGREG_LABEL0,
    RDMSGREG_CREATE_ACK_RES_ID,
    RDMSGREG_CREATE_ACK_DEST,
    RDMSGREG_CREATE_ACK_END,

    // Other ramdisk server API calls have no arguments
};