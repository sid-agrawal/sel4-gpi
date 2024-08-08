#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/gpi_rpc.h>
#include <fs_rpc.pb.h>

#include <fs_shared.h>
#include <fs_client.h>
#include <muslcsys/vsyscall.h>

#define FD_TABLE_SIZE 32
#define FS_APP "fs_server"
#define DEV_NULL_PATH "/dev/null"
static int dev_null_fd;

#if FS_DEBUG_ENABLED
#define XV6FS_PRINTF(...)   \
  do                        \
  {                         \
    printf("%s ", XV6FS_C); \
    printf(__VA_ARGS__);    \
  } while (0);
#else
#define XV6FS_PRINTF(...)
#endif

#define CHECK_ERROR(error, msg) \
  do                            \
  {                             \
    if (error != seL4_NoError)  \
    {                           \
      ZF_LOGE(XV6FS_C "%s: %s"  \
                      ", %d.",  \
              __func__,         \
              msg,              \
              error);           \
      return error;             \
    }                           \
  } while (0);

#define CHECK_ERROR_EXIT(error, msg) \
  do                                 \
  {                                  \
    if (error != seL4_NoError)       \
    {                                \
      ZF_LOGE(XV6FS_C "%s: %s"       \
                      ", %d.",       \
              __func__,              \
              msg,                   \
              error);                \
      goto exit;                     \
    }                                \
  } while (0);

/* RPC environment */
static sel4gpi_rpc_env_t rpc_client = {
    .request_desc = &FsMessage_msg,
    .reply_desc = &FsReturnMessage_msg,
};

/* Simple FD functions */
xv6fs_client_context_t fd_table[FD_TABLE_SIZE];
xv6fs_client_context_t xv6fs_null_client_context;

// Initialize the FD table
static void fd_init(void)
{
  memset(&xv6fs_null_client_context, 0, sizeof(xv6fs_client_context_t));
  memset(fd_table, 0, sizeof(xv6fs_client_context_t) * FD_TABLE_SIZE);
  dev_null_fd = 4;
}

// Add a file to the file descriptor table
int fd_bind(seL4_CPtr file_cap)
{
  for (int i = 5; i < FD_TABLE_SIZE; i++)
    if (fd_table[i].ep == 0)
    {
      fd_table[i].ep = file_cap;
      fd_table[i].offset = 0;
      // printf("%s: new fd %d\n", __func__, i);
      return i;
    }
  return -1;
}

// Get a file by id
xv6fs_client_context_t *fd_get(int fd)
{
  if (fd >= 0 && fd < FD_TABLE_SIZE)
  {
    if (fd_table[fd].ep == 0)
    {
      return NULL;
    }
    return &fd_table[fd];
  }
  return NULL;
}

void fd_close(int fd)
{
  int error = 0;

  if (fd >= 0 && fd < FD_TABLE_SIZE)
  {
    // No need to free the old cap, it will be revoked by the file system
    fd_table[fd] = xv6fs_null_client_context;
  }
}

/* FS Client */

/* This must be publically accessible, for SpaceJMP example */
global_xv6fs_client_context_t xv6fs_client;

global_xv6fs_client_context_t *get_xv6fs_client(void)
{
  return &xv6fs_client;
}

int start_xv6fs_pd(gpi_space_id_t rd_id,
                   seL4_CPtr *fs_pd_cap,
                   gpi_space_id_t *fs_id)
{
  int error = start_resource_server_pd(sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME), rd_id,
                                       FS_APP, fs_pd_cap, fs_id);
  CHECK_ERROR(error, "failed to start file resource server\n");
  XV6FS_PRINTF("Successfully started file system server\n");
  return 0;
}

int xv6fs_client_set_namespace(gpi_space_id_t ns_id)
{
  XV6FS_PRINTF("Client of FS server will use namespace %u\n", ns_id);

  get_xv6fs_client()->space_id = ns_id;

  /* Get server EP */
  seL4_CPtr server_ep = sel4gpi_get_rde_by_space_id(get_xv6fs_client()->space_id, get_xv6fs_client()->file_cap_type);
  get_xv6fs_client()->server_ep = server_ep;

  return 0;
}

int xv6fs_client_get_file(int fd, seL4_CPtr *file_ep)
{
  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_client_get_file: Invalid FD provided\n");
    return -1;
  }

  *file_ep = file->ep;
  return 0;
}

// Not used for libc
int xv6fs_client_link_file(seL4_CPtr file, const char *pathname)
{
  XV6FS_PRINTF("fs_link_file file cptr %d, path %s\n", (int)file, pathname);

  int error = 0;
  assert(strlen(pathname) <= MAXPATH);

  // Send IPC to fs server
  seL4_CPtr caps[1] = {file};

  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_link_tag};
  strncpy(msg.msg.link.path, pathname, MAXPATH);

  FsReturnMessage ret_msg = {0};

  error = sel4gpi_rpc_call(&rpc_client, get_xv6fs_client()->server_ep, &msg, 1, caps, &ret_msg);

  return error || ret_msg.errorCode;
}

/* Remote fs access functions to override libc fs ops */
static int xv6fs_libc_open(const char *pathname, int flags, int modes)
{
  XV6FS_PRINTF("xv6fs_libc_open pathname %s, flags 0x%x\n", pathname, flags);

  int error;
  assert(strlen(pathname) <= MAXPATH);

  // Check for /dev/null
  if (strcmp(pathname, DEV_NULL_PATH) == 0)
  {
    return dev_null_fd;
  }

  // Send IPC to fs server
  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_create_tag,
      .msg.create = {
          .flags = flags,
          // (XXX) Currently ignore modes
      }};
  strncpy(msg.msg.create.path, pathname, MAXPATH);

  FsReturnMessage ret_msg = {0};

  error = sel4gpi_rpc_call(&rpc_client, get_xv6fs_client()->server_ep, &msg, 0, NULL, &ret_msg);

  if (error || ret_msg.errorCode)
  {
    return -1;
  }

  // Add file to FD table
  seL4_CPtr dest = ret_msg.msg.create.slot;
  int fd = fd_bind(dest);

  if (fd == -1)
  {
    XV6FS_PRINTF("Ran out of slots in the FD table\n");
    return -1;
  }

  XV6FS_PRINTF("Opened fd %d\n", fd);

  return fd;
}

static int xv6fs_libc_pread(int fd, void *buf, int count, int offset)
{
  int error = 0;

  XV6FS_PRINTF("xv6fs_libc_read fd %d len %d offset %d\n", fd, count, offset);

  if (count > RAMDISK_BLOCK_SIZE)
  {
    // (XXX) Arya: Support larger file read/writes
    XV6FS_PRINTF("Count too large for read\n");
    return -1;
  }

  // Check for /dev/null
  if (fd == dev_null_fd)
  {
    return -1;
  }

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_libc_pread: Invalid FD provided\n");
    return -1;
  }

  // Send IPC to fs server
  seL4_CPtr caps[1] = {get_xv6fs_client()->shared_mem->ep};

  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_read_tag,
      .msg.read = {
          .n = count,
          .offset = offset,
      }};

  FsReturnMessage ret_msg = {0};

  error = sel4gpi_rpc_call(&rpc_client, file->ep,
                           &msg, 1, caps, &ret_msg);

  if (error || ret_msg.errorCode)
  {
    return -1;
  }

  // Copy from shared mem to buf
  int bytes_read = ret_msg.msg.read.n;
  if (bytes_read > 0)
  {
    memcpy(buf, get_xv6fs_client()->shared_mem_vaddr, bytes_read);
  }

  return bytes_read;
}

static int xv6fs_libc_read(int fd, void *buf, int count)
{
  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_libc_read: Invalid FD provided\n");
    return -1;
  }

  // Read at current offset
  int bytes_read = xv6fs_libc_pread(fd, buf, count, file->offset);

  // Update file offset
  if (bytes_read > 0)
  {
    file->offset += bytes_read;
  }

  return bytes_read;
}

static int xv6fs_libc_write(int fd, const void *buf, int count)
{
  XV6FS_PRINTF("xv6fs_libc_write fd %d len %d\n", fd, count);
  int error = 0;

  // Check for /dev/null
  if (fd == dev_null_fd)
  {
    return 0;
  }

  if (count > RAMDISK_BLOCK_SIZE)
  {
    // (XXX) Arya: Support larger file read/writes
    XV6FS_PRINTF("Count too large for read\n");
    return -1;
  }

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_libc_write: Invalid FD provided\n");
    return -1;
  }

  // Copy from buf to shared mem
  if (count > 0)
  {
    memcpy(get_xv6fs_client()->shared_mem_vaddr, buf, count);
  }

  // Send IPC to fs server
  seL4_CPtr caps[1] = {get_xv6fs_client()->shared_mem->ep};

  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_write_tag,
      .msg.write = {
          .n = count,
          .offset = file->offset,
      }};

  FsReturnMessage ret_msg = {0};

  error = sel4gpi_rpc_call(&rpc_client, file->ep,
                           &msg, 1, caps, &ret_msg);

  if (error || ret_msg.errorCode)
  {
    return -1;
  }

  // Update file offset
  int bytes_written = ret_msg.msg.write.n;
  if (bytes_written > 0)
  {
    file->offset += bytes_written;
  }

  return bytes_written;
}

static int xv6fs_libc_close(int fd)
{
  XV6FS_PRINTF("xv6fs_libc_close fd %d\n", fd);

  int error = 0;

  // Check for /dev/null
  if (fd == dev_null_fd)
  {
    return 0;
  }

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_libc_close: Invalid FD provided\n");
    return -1;
  }

  // Send IPC to fs server
  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_close_tag,
  };

  FsReturnMessage ret_msg = {0};

  error = sel4gpi_rpc_call(&rpc_client, file->ep,
                           &msg, 0, NULL, &ret_msg);

  if (error || ret_msg.errorCode)
  {
    XV6FS_PRINTF("Server failed to close file\n");
    return -1;
  }

  // Close the FD locally
  fd_close(fd);

  return error;
}

static int xv6fs_libc_lseek(int fd, off_t offset, int whence)
{
  XV6FS_PRINTF("xv6fs_libc_lseek fd %d offset %lu whence %d\n", fd, offset, whence);

  // Handle file offset locally

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_libc_lseek: Invalid FD provided\n");
    return -1;
  }

  // Manage the offset
  switch (whence)
  {
  case SEEK_SET:
    file->offset = offset;
    break;
  case SEEK_CUR:
    file->offset += offset;
    break;
  case SEEK_END:
    // Can't handle seek_end in client
  default:
    XV6FS_PRINTF("Unsupported whence for lseek\n");
    return -1;
  }

  return file->offset;
}

char *xv6fs_libc_getcwd(char *buf, size_t size)
{
  // (XXX) File system does not currently support directories
  strncpy(buf, ROOT_DIR, size);
  return buf;
}

int xv6fs_libc_fstat(int fd, struct stat *buf)
{
  XV6FS_PRINTF("xv6fs_libc_fstat fd %d\n", fd);
  int error = 0;

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_libc_fstat: Invalid FD provided\n");
    return -1;
  }

  // Send IPC to fs server
  seL4_CPtr caps[1] = {get_xv6fs_client()->shared_mem->ep};

  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_stat_tag,
  };

  FsReturnMessage ret_msg = {0};

  error = sel4gpi_rpc_call(&rpc_client, file->ep,
                           &msg, 1, caps, &ret_msg);

  if (error || ret_msg.errorCode)
  {
    return -1;
  }

  // Copy from shared mem to buf
  memcpy(buf, get_xv6fs_client()->shared_mem_vaddr, sizeof(struct stat));

  return 0;
}

int xv6fs_libc_stat(const char *pathname, struct stat *buf)
{
  XV6FS_PRINTF("xv6fs_libc_stat pathname %s\n", pathname);

  int fd = xv6fs_libc_open(pathname, O_RDWR, 0);
  if (fd == -1)
  {
    XV6FS_PRINTF("xv6fs_libc_stat returning -1, file does not exist\n");
    return -ENOENT;
  }

  int error = xv6fs_libc_fstat(fd, buf);

  xv6fs_libc_close(fd);

  return error;
}

static int xv6fs_libc_fcntl(int fd, int cmd, ...)
{
  XV6FS_PRINTF("xv6fs_libc_fcntl fd %d cmd %d\n", fd, cmd);

  // Get extra arg provided for some fcntl commands
  unsigned long arg;
  va_list ap;
  va_start(ap, cmd);
  arg = va_arg(ap, unsigned long);
  va_end(ap);

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_libc_fcntl: Invalid FD provided\n");
    return -1;
  }

  int ret = 0;
  switch (cmd)
  {
  case F_SETFL:
    uint64_t flags_mask = O_APPEND | O_ASYNC | O_NONBLOCK;
    file->flags = (file->flags & ~flags_mask) | (arg & flags_mask);
    break;
  case F_GETFL:
    ret = file->flags;
    break;
  case F_GETOWN:
    XV6FS_PRINTF("xv6fs_sys_fcntl: Unsupported cmd F_GETOWN\n");
    ret = -1;
    break;
  case F_DUPFD_CLOEXEC:
    XV6FS_PRINTF("xv6fs_sys_fcntl: Unsupported cmd F_DUPFD_CLOEXEC\n");
    ret = -1;
    break;
  case F_SETLK:
    // (XXX) Ignoring file lock operations
    break;
  case F_SETLKW:
    // (XXX) Ignoring file lock operations
    break;
  case F_GETLK:
    // (XXX) Ignoring file lock operations
    struct flock *lk = (struct flock *)arg;
    lk->l_type = F_UNLCK;
    break;
  case F_GETOWN_EX:
    XV6FS_PRINTF("xv6fs_sys_fcntl: Unsupported cmd F_GETOWN_EX\n");
    ret = -1;
    break;
  case F_SETOWN_EX:
    XV6FS_PRINTF("xv6fs_sys_fcntl: Unsupported cmd F_SETOWN_EX\n");
    ret = -1;
    break;
  default:
    XV6FS_PRINTF("xv6fs_sys_fcntl: Unknown cmd %d\n", cmd);
    ret = -1;
    break;
  }

  return 0;
}

static int xv6fs_libc_unlink(const char *pathname)
{
  XV6FS_PRINTF("xv6fs_libc_unlink pathname %s\n", pathname);

  int error = 0;
  assert(strlen(pathname) <= MAXPATH);

  // Copy pathname from buf to shared mem
  strcpy(get_xv6fs_client()->shared_mem_vaddr, pathname);

  // Send IPC to fs server
  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_unlink_tag,
  };
  strncpy(msg.msg.unlink.path, pathname, MAXPATH);

  FsReturnMessage ret_msg = {0};

  error = sel4gpi_rpc_call(&rpc_client, get_xv6fs_client()->server_ep, &msg, 0, NULL, &ret_msg);

  if (error || ret_msg.errorCode)
  {
    error = -1;
  }

  return error;
}

static int xv6fs_libc_access(const char *pathname, int amode)
{
  XV6FS_PRINTF("xv6fs_libc_access pathname %s amode 0x%x\n", pathname, amode);
  int error = 0;

  int flags = 0;
  if (amode & R_OK)
  {
    if (amode & W_OK)
    {
      flags |= O_RDWR;
    }
    else
    {
      flags |= O_RDONLY;
    }
  }

  int fd = xv6fs_libc_open(pathname, flags, 0);
  error = xv6fs_libc_close(fd);

  if (fd != -1)
  {
    error = xv6fs_libc_close(fd);
    if (error)
    {
      XV6FS_PRINTF("xv6fs_libc_access failed to close FD %d\n", fd);
    }
  }

  return fd != -1;
}

static int xv6fs_libc_faccessat(int dirfd, const char *filename, int amode, int flags)
{
  XV6FS_PRINTF("xv6fs_libc_faccessat path %s, mode %d, flag %x\n", filename, mode, flags);

  if (dirfd != AT_FDCWD)
  {
    ZF_LOGE("faccessat only supports relative path to the current working directory\n");
    return -EINVAL;
  }

  if (flags != 0 && flags != AT_SYMLINK_NOFOLLOW)
  {
    ZF_LOGE("faccessat does not support flags 0x%x\n", flags);
    return -EINVAL;
  }

  return xv6fs_libc_access(filename, amode);
}

static int xv6fs_libc_fsync(int fd)
{
  XV6FS_PRINTF("xv6fs_libc_fsync fd %d\n", fd);

  // Do nothing

  return 0;
}

static int xv6fs_libc_fchmod(int fd, mode_t mode)
{
  XV6FS_PRINTF("xv6fs_libc_fchmod fd %d mode %d\n", fd, mode);

  // Do nothing

  return 0;
}

static int xv6fs_libc_chown(const char *path, uid_t uid, gid_t gid)
{
  XV6FS_PRINTF("xv6fs_libc_chown path %s, uid %d, gid %d\n", path, uid, gid);

  // Do nothing

  return 0;
}

static int xv6fs_libc_geteuid(void)
{
  XV6FS_PRINTF("xv6fs_libc_geteuid\n");

  // Do nothing

  return 1;
}

int xv6fs_client_new_ns(gpi_space_id_t *ns_id)
{
  XV6FS_PRINTF("Requesting new namespace from server ep\n");

  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_ns_tag,
  };

  FsReturnMessage ret_msg = {0};

  int error = sel4gpi_rpc_call(&rpc_client, get_xv6fs_client()->server_ep, (void *)&msg, 0, NULL, (void *)&ret_msg);
  error |= ret_msg.errorCode;

  if (!error)
  {
    *ns_id = ret_msg.msg.ns.space_id;
  }

  return error;
}

int xv6fs_client_delete_ns(seL4_CPtr ns_ep)
{
  XV6FS_PRINTF("Requesting new namespace from server ep\n");

  FsMessage msg = {
      .magic = FS_RPC_MAGIC,
      .which_msg = FsMessage_delete_ns_tag,
  };

  FsReturnMessage ret_msg = {0};

  int error = sel4gpi_rpc_call(&rpc_client, ns_ep, (void *)&msg, 0, NULL, (void *)&ret_msg);
  error |= ret_msg.errorCode;
  return error;
}

/** ENTRY POINTS FOR MUSLC **/
static long xv6fs_muslcsys_open(va_list ap)
{
  const char *pathname = va_arg(ap, const char *);
  int flags = va_arg(ap, int);
  mode_t mode = va_arg(ap, mode_t);

  return xv6fs_libc_open(pathname, flags, mode);
}

static long xv6fs_muslcsys_openat(va_list ap)
{
  int dirfd = va_arg(ap, int);
  const char *pathname = va_arg(ap, const char *);
  int flags = va_arg(ap, int);
  mode_t mode = va_arg(ap, mode_t);

  if (dirfd != AT_FDCWD)
  {
    ZF_LOGE("Openat only supports relative path to the current working directory\n");
    return -EINVAL;
  }

  return xv6fs_libc_open(pathname, flags, mode);
}

static long xv6fs_muslcsys_close(va_list ap)
{
  int fd = va_arg(ap, int);

  return xv6fs_libc_close(fd);
}

/* Writev syscall implementation for muslc. Only implemented for stdin and stdout. */
static long xv6fs_muslcsys_writev(va_list ap)
{
  ZF_LOGE("CellulOS FS does not support sys_writev\n");
  return -1;
}

static long xv6fs_muslcsys_write(va_list ap)
{

  int fd = va_arg(ap, int);
  void *buf = va_arg(ap, void *);
  size_t count = va_arg(ap, size_t);

  return xv6fs_libc_write(fd, buf, count);
}

static long xv6fs_muslcsys_readv(va_list ap)
{
  ZF_LOGE("CellulOS FS does not support sys_readv\n");
  return -1;
}

static long xv6fs_muslcsys_read(va_list ap)
{
  int fd = va_arg(ap, int);
  void *buf = va_arg(ap, void *);
  size_t count = va_arg(ap, size_t);

  return xv6fs_libc_read(fd, buf, count);
}

static long xv6fs_muslcsys_lseek(va_list ap)
{
  int fd = va_arg(ap, int);
  off_t offset = va_arg(ap, off_t);
  int whence = va_arg(ap, int);

  return xv6fs_libc_lseek(fd, offset, whence);
}

static long xv6fs_muslcsys__llseek(va_list ap)
{
  ZF_LOGE("CellulOS FS does not support sys_llseek\n");
  return -1;
}

static long xv6fs_muslcsys_access(va_list ap)
{
  const char *pathname = va_arg(ap, const char *);
  int mode = va_arg(ap, int);

  return xv6fs_libc_access(pathname, mode);
}

static long xv6fs_muslcsys_faccessat(va_list ap)
{
  int fd = va_arg(ap, int);
  const char *pathname = va_arg(ap, const char *);
  int mode = va_arg(ap, int);
  int flags = va_arg(ap, int);

  return xv6fs_libc_faccessat(fd, pathname, mode, flags);
}

static long xv6fs_muslcsys_stat(va_list ap)
{
  const char *path = va_arg(ap, const char *);
  struct stat *buf = va_arg(ap, struct stat *);

  return xv6fs_libc_stat(path, buf);
}

static long xv6fs_muslcsys_fstat(va_list ap)
{
  int fd = va_arg(ap, int);
  struct stat *buf = va_arg(ap, struct stat *);

  return xv6fs_libc_fstat(fd, buf);
}

static long xv6fs_muslcsys_fstatat(va_list ap)
{
  int dirfd = va_arg(ap, int);
  char *path = va_arg(ap, char *);
  struct stat *buf = va_arg(ap, struct stat *);
  int flags = va_arg(ap, int);

  if (dirfd != AT_FDCWD)
  {
    ZF_LOGE("Fstatat only supports relative path to the current working directory\n");
    return -EINVAL;
  }

  if (flags != 0 && flags != AT_SYMLINK_NOFOLLOW)
  {
    ZF_LOGE("Fstatat does not support flags 0x%x\n", flags);
    return -EINVAL;
  }

  return xv6fs_libc_stat(path, buf);
}

static long xv6fs_muslcsys_getcwd(va_list ap)
{
  char *buf = va_arg(ap, char *);
  size_t size = va_arg(ap, size_t);

  return xv6fs_libc_getcwd(buf, size) != NULL;
}

static long xv6fs_muslcsys_fcntl(va_list ap)
{
  int fd = va_arg(ap, int);
  int cmd = va_arg(ap, int);
  long arg = va_arg(ap, long);

  return xv6fs_libc_fcntl(fd, cmd, arg);
}

static long xv6fs_muslcsys_unlink(va_list ap)
{
  const char *path = va_arg(ap, const char *);

  return xv6fs_libc_unlink(path);
}

static long xv6fs_muslcsys_unlinkat(va_list ap)
{
  int dirfd = va_arg(ap, int);
  const char *path = va_arg(ap, const char *);
  int flags = va_arg(ap, int);

  if (dirfd != AT_FDCWD)
  {
    ZF_LOGE("Unlinkat only supports relative path to the current working directory\n");
    return -EINVAL;
  }

  if (flags != 0)
  {
    ZF_LOGE("Unlinkat does not support flags, but set to 0x%x\n", flags);
    return -EINVAL;
  }

  return xv6fs_libc_unlink(path);
}

static long xv6fs_muslcsys_pread(va_list ap)
{
  int fd = va_arg(ap, int);
  void *buf = va_arg(ap, void *);
  size_t size = va_arg(ap, size_t);
  off_t ofs = va_arg(ap, off_t);

  return xv6fs_libc_pread(fd, buf, size, ofs);
}

static long xv6fs_muslcsys_fsync(va_list ap)
{
  int fd = va_arg(ap, int);

  return xv6fs_libc_fsync(fd);
}

static long xv6fs_muslcsys_fchmod(va_list ap)
{
  int fd = va_arg(ap, int);
  mode_t mode = va_arg(ap, mode_t);

  return xv6fs_libc_fchmod(fd, mode);
}

static long xv6fs_muslcsys_chown(va_list ap)
{
  char *path = va_arg(ap, char *);
  uid_t uid = va_arg(ap, uid_t);
  gid_t gid = va_arg(ap, gid_t);

  return xv6fs_libc_chown(path, uid, gid);
}

static long xv6fs_muslcsys_geteuid(va_list ap)
{
  return xv6fs_libc_geteuid();
}

seL4_Error
xv6fs_client_init(void)
{
  XV6FS_PRINTF("Initializing client of FS server\n");

  // Do not re-initialize
  if (get_xv6fs_client()->shared_mem_vaddr != NULL)
  {
    return 0;
  }

  int error;

  get_xv6fs_client()->file_cap_type = sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME);
  xv6fs_client_set_namespace(BADGE_SPACE_ID_NULL);

  /* Allocate the shared memory object */
  get_xv6fs_client()->shared_mem = malloc(sizeof(mo_client_context_t));

  error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                      1,
                                      MO_PAGE_BITS,
                                      get_xv6fs_client()->shared_mem);
  CHECK_ERROR(error, "failed to allocate shared mem page");

  seL4_CPtr vmr_rde = sel4gpi_get_bound_vmr_rde();
  error = vmr_client_attach_no_reserve(vmr_rde,
                                       NULL,
                                       get_xv6fs_client()->shared_mem,
                                       SEL4UTILS_RES_TYPE_SHARED_FRAMES,
                                       &get_xv6fs_client()->shared_mem_vaddr);
  CHECK_ERROR(error, "failed to map shared mem page for fs client");

  /* Setup local FD data structure */
  fd_init();

  /* Install the syscalls with sel4muslcsys */

  /* Implemented functions */
  muslcsys_install_syscall(__NR_write, xv6fs_muslcsys_write);
#ifdef __NR_open
  muslcsys_install_syscall(__NR_open, xv6fs_muslcsys_open);
#endif
#ifdef __NR_openat
  muslcsys_install_syscall(__NR_openat, xv6fs_muslcsys_openat);
#endif
  muslcsys_install_syscall(__NR_close, xv6fs_muslcsys_close);
  muslcsys_install_syscall(__NR_readv, xv6fs_muslcsys_readv);
  muslcsys_install_syscall(__NR_read, xv6fs_muslcsys_read);
#ifdef __NR_access
  muslcsys_install_syscall(__NR_access, xv6fs_muslcsys_access);
#endif
#ifdef __NR_faccessat
  muslcsys_install_syscall(__NR_faccessat, xv6fs_muslcsys_faccessat);
#endif
  muslcsys_install_syscall(__NR_fstatat, xv6fs_muslcsys_fstatat);
  muslcsys_install_syscall(__NR_fstat, xv6fs_muslcsys_fstat);
  muslcsys_install_syscall(__NR_getcwd, xv6fs_muslcsys_getcwd);
  muslcsys_install_syscall(__NR_fcntl, xv6fs_muslcsys_fcntl);
  muslcsys_install_syscall(__NR_unlinkat, xv6fs_muslcsys_unlinkat);
  muslcsys_install_syscall(__NR_pread64, xv6fs_muslcsys_pread);
  muslcsys_install_syscall(__NR_lseek, xv6fs_muslcsys_lseek);

  /* No-ops */
  muslcsys_install_syscall(__NR_fsync, xv6fs_muslcsys_fsync);
  muslcsys_install_syscall(__NR_fchmod, xv6fs_muslcsys_fchmod);
  // muslcsys_install_syscall(__NR_chown, xv6fs_muslcsys_chown);
  muslcsys_install_syscall(__NR_geteuid, xv6fs_muslcsys_geteuid);

  return error;
}