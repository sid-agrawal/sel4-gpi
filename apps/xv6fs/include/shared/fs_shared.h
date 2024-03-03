#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>
#include <ramdisk_shared.h>

/* General file system configuration */
#define FS_DEBUG 0
#define FS_SIZE (RAMDISK_SIZE_BYTES / RAMDISK_BLOCK_SIZE) // Size of file system in blocks
#define BSIZE RAMDISK_BLOCK_SIZE                          // Block size in bytes
#define N_INODES FS_SIZE / 4                              // Max number of inodes in the filesystem
#define ROOT_DIR "/"                                      // Name of the initial directory
#define NDEV 10                                           // maximum major device number
#define ROOTDEV 1                                         // device number of file system root disk
#define MAXOPBLOCKS 10                                    // max # of blocks any FS op writes
#define LOGSIZE 0                                         // use no log, previous value was (MAXOPBLOCKS * 3)
#define NBUF (MAXOPBLOCKS * 3)                            // size of disk block cache
#define MAXPATH 128                                       // maximum file path name

/* API of the fs server */

/* IPC values returned in the "label" message header. */
enum fs_errors
{
    FS_SERVER_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    FS_SERVER_ERROR_UNKNOWN = seL4_NumErrors,
    FS_SERVER_ERROR_BADGE,
    FS_SERVER_ERROR_NOFILE
};

/* IPC Message register values for FSMSGREG_FUNC */
enum fs_server_funcs
{
    FS_FUNC_CREATE_REQ = 0,
    FS_FUNC_CREATE_ACK,

    FS_FUNC_READ_REQ,
    FS_FUNC_READ_ACK,

    FS_FUNC_WRITE_REQ,
    FS_FUNC_WRITE_ACK,

    FS_FUNC_CLOSE_REQ,
    FS_FUNC_CLOSE_ACK,

    FS_FUNC_UNLINK_REQ,
    FS_FUNC_UNLINK_ACK,

    FS_FUNC_STAT_REQ,
    FS_FUNC_STAT_ACK,
};

/* Designated purposes of each message register in the mini-protocol. */
enum fs_msgregs
{
    /* These are fixed headers in every fs message. */
    FSMSGREG_FUNC = 0,

    /* This is a convenience label for IPC MessageInfo length. */
    FSMSGREG_LABEL0,

    /* Create */
    FSMSGREG_CREATE_REQ_FLAGS = FSMSGREG_LABEL0,
    FSMSGREG_CREATE_REQ_END,
    FSMSGREG_CREATE_ACK_END = FSMSGREG_LABEL0,

    /* Read */
    FSMSGREG_READ_REQ_N = FSMSGREG_LABEL0,
    FSMSGREG_READ_REQ_OFFSET,
    FSMSGREG_READ_REQ_END,
    FSMSGREG_READ_ACK_N = FSMSGREG_LABEL0,
    FSMSGREG_READ_ACK_END,

    /* Write */
    FSMSGREG_WRITE_REQ_N = FSMSGREG_LABEL0,
    FSMSGREG_WRITE_REQ_OFFSET,
    FSMSGREG_WRITE_REQ_END,
    FSMSGREG_WRITE_ACK_N = FSMSGREG_LABEL0,
    FSMSGREG_WRITE_ACK_END,

    /* Close */
    FSMSGREG_CLOSE_REQ_FLAGS = FSMSGREG_LABEL0,
    FSMSGREG_CLOSE_ACK_END = FSMSGREG_LABEL0,

    /* Unlink */
    FSMSGREG_UNLINK_REQ_END = FSMSGREG_LABEL0,
    FSMSGREG_UNLINK_ACK_END = FSMSGREG_LABEL0,

    /* Stat */
    FSMSGREG_STAT_REQ_END = FSMSGREG_LABEL0,
    FSMSGREG_STAT_ACK_END = FSMSGREG_LABEL0,

};