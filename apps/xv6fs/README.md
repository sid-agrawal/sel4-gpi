## libxv6fs

A port of the filesystem from [xv6](https://github.com/mit-pdos/xv6-riscv) for seL4. Runs a file system server supporting the following operations:
- open
- read
- write
- stat
- fstat
- lseek
- close
- unlink

Usage with libc:
- `xv6fs.c` populates a global `libc_fs_ops` structure with the corresponding `xv6fs` functions
- Requires that libc be modified to call the appropriate `libc_fs_ops` functions.