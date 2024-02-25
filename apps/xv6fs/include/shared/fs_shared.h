#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>
#include <ramdisk_shared.h>

/* General file system configuration */
#define FS_SIZE (RAMDISK_SIZE_BYTES / RAMDISK_BLOCK_SIZE) // Size of file system in blocks
#define BSIZE RAMDISK_BLOCK_SIZE                          // Block size in bytes
#define N_INODES FS_SIZE / 4                              // Max number of inodes in the filesystem
#define ROOT_DIR "./"                                     // Name of the initial directory
#define NFILE 100                                         // Number of files in open file table
#define NDEV 10                                           // maximum major device number
#define ROOTDEV 1                                         // device number of file system root disk
#define MAXOPBLOCKS 10                                    // max # of blocks any FS op writes
#define LOGSIZE (MAXOPBLOCKS * 3)                         // max data blocks in on-disk log
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
    FSMSGREG_CREATE_REQ_END = FSMSGREG_LABEL0,
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

    /* Stat */
    FSMSGREG_STAT_REQ_END = FSMSGREG_LABEL0,
    FSMSGREG_STAT_ACK_END = FSMSGREG_LABEL0,

};

// (XXX) ARYA: to remove once swapped to new api

/* Memory regions for IPC to xv6fs server */
#define XV6FS_OP 0

// open
#define XV6FS_FLAGS 1
#define XV6FS_MODE 2

// read / write
#define XV6FS_FD 1
#define XV6FS_COUNT 2
#define XV6FS_POFFSET 3

// seek
#define XV6FS_OFFSET 2
#define XV6FS_WHENCE 3

// getcwd
#define XV6FS_SIZE 1

// fcntl
#define XV6FS_CMD 2
#define XV6FS_ARG 3

// return values
#define XV6FS_RET 0

/* xv6fs opcodes */
enum xv6fsOp
{
    XV6FS_REGISTER_CLIENT = 0,
    XV6FS_OPEN,
    XV6FS_READ,
    XV6FS_WRITE,
    XV6FS_STAT,
    XV6FS_FSTAT,
    XV6FS_LSEEK,
    XV6FS_CLOSE,
    XV6FS_UNLINK,
    XV6FS_GETCWD,
    XV6FS_FCNTL,
    XV6FS_PREAD,
    XV6FS_PWRITE
};