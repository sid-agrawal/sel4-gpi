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
enum xv6fsOp {
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