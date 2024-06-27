#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>
#include <ramdisk_shared.h>

/* General file system configuration */
#define FILE_RESOURCE_TYPE_NAME "FILE"
#define FS_DEBUG_ENABLED 0 // (XXX) Arya: Migrate to the GPI debug setting
/* (XXX) Arya: Make size configurable, currently just allows 2 FS per ramdisk */
#define FS_SIZE (RAMDISK_SIZE_BYTES / RAMDISK_BLOCK_SIZE) / 2 // Size of file system in blocks
#define BSIZE RAMDISK_BLOCK_SIZE                              // Block size in bytes
#define N_INODES FS_SIZE / 2                                  // Max number of inodes in the filesystem
#define ROOT_DIR "/"                                          // Name of the initial directory
#define NDEV 10                                               // maximum major device number
#define ROOTDEV 1                                             // device number of file system root disk
#define MAXOPBLOCKS 10                                        // max # of blocks any FS op writes
#define LOGSIZE 0                                             // use no log, previous value was (MAXOPBLOCKS * 3)
#define NBUF (MAXOPBLOCKS * 3)                                // size of disk block cache
#define MAXPATH 128                                           // maximum file path name