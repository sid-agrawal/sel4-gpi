#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_remote_utils.h>
#include <sel4gpi/pd_utils.h>

#include <libc_fs_helpers.h>
#include <fs_shared.h>
#include <fs_client.h>

#define FD_TABLE_SIZE 32
#define FS_APP "fs_server"
#define DEV_NULL_PATH "/dev/null"
static int dev_null_fd;

#if FS_DEBUG
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
    if (fd_table[i].badged_server_ep_cspath.capPtr == 0)
    {
      fd_table[i].badged_server_ep_cspath.capPtr = file_cap;
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
    if (fd_table[fd].badged_server_ep_cspath.capPtr == 0)
    {
      return NULL;
    }
    return &fd_table[fd];
  }
  return NULL;
}

void fd_close(int fd)
{
  if (fd >= 0 && fd < FD_TABLE_SIZE)
  {
    // (XXX) Arya: free the badged_server_ep_cspath.capPtr
    fd_table[fd] = xv6fs_null_client_context;
  }
}

/* FS Client */
static void init_global_libc_fs_ops(void);

/* This must be publically accessible, for SpaceJMP example */
global_xv6fs_client_context_t xv6fs_client;

global_xv6fs_client_context_t *get_xv6fs_client(void)
{
  return &xv6fs_client;
}

int start_xv6fs_pd(uint64_t rd_id,
                   seL4_CPtr *fs_pd_cap,
                   uint64_t *fs_id)
{
  int error = start_resource_server_pd(GPICAP_TYPE_BLOCK, rd_id,
                                       FS_APP, fs_pd_cap, fs_id);
  CHECK_ERROR(error, "failed to start file resource server\n");
  XV6FS_PRINTF("Successfully started file system server\n");
  return 0;
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

  get_xv6fs_client()->space_id = RESSPC_ID_NULL;

  /* Allocate the TEMP shared memory object */
  get_xv6fs_client()->shared_mem = malloc(sizeof(mo_client_context_t));
  seL4_CPtr free_slot;
  pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
  error = pd_client_next_slot(&pd_conn, &free_slot);
  CHECK_ERROR(error, "failed to get next cspace slot");

  error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                      free_slot,
                                      1,
                                      get_xv6fs_client()->shared_mem);
  CHECK_ERROR(error, "failed to allocate shared mem page");

  ads_client_context_t vmr_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR)};
  error = ads_client_attach(&vmr_rde,
                            NULL,
                            get_xv6fs_client()->shared_mem,
                            SEL4UTILS_RES_TYPE_SHARED_FRAMES,
                            &get_xv6fs_client()->shared_mem_vaddr);
  CHECK_ERROR(error, "failed to map shared mem page for fs client");

  /* Setup local FD data structure */
  fd_init();

  /* Override libc fs ops */
  init_global_libc_fs_ops();

  return error;
}

int xv6fs_client_set_namespace(uint64_t ns_id)
{
  XV6FS_PRINTF("Client of FS server will use namespace %ld\n", ns_id);

  // seL4_CPtr ep = sel4gpi_get_rde_by_space_id(ns_id, GPICAP_TYPE_FILE);

  // if (ep == seL4_CapNull)
  // {
  //   return -1;
  // }

  get_xv6fs_client()->space_id = ns_id;
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

  *file_ep = file->badged_server_ep_cspath.capPtr;
  return 0;
}

// Not used for libc
int xv6fs_client_link_file(seL4_CPtr file, const char *path)
{
  XV6FS_PRINTF("fs_link_file file cptr %d, path %s\n", (int)file, path);

  int error;

  // Copy pathname from buf to shared mem
  strcpy(get_xv6fs_client()->shared_mem_vaddr, path);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2, FSMSGREG_LINK_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_LINK_REQ);
  seL4_SetCap(0, get_xv6fs_client()->shared_mem->badged_server_ep_cspath.capPtr);
  seL4_SetCap(1, file);

  tag = seL4_Call(sel4gpi_get_rde_by_space_id(get_xv6fs_client()->space_id, GPICAP_TYPE_FILE), tag);

  return seL4_MessageInfo_get_label(tag);
}

/* Remote fs access functions to override libc fs ops */
static int xv6fs_libc_open(const char *pathname, int flags, int modes)
{
  XV6FS_PRINTF("xv6fs_libc_open pathname %s, flags 0x%x\n", pathname, flags);

  int error;

  // Check for /dev/null
  if (strcmp(pathname, DEV_NULL_PATH) == 0)
  {
    return dev_null_fd;
  }

  // Copy pathname from buf to shared mem
  strcpy(get_xv6fs_client()->shared_mem_vaddr, pathname);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_CREATE_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_CREATE_REQ);
  seL4_SetMR(FSMSGREG_CREATE_REQ_FLAGS, flags);
  seL4_SetCap(0, get_xv6fs_client()->shared_mem->badged_server_ep_cspath.capPtr);
  // (XXX) Currently ignore modes

  // Alloc received cap ep
  tag = seL4_Call(sel4gpi_get_rde_by_space_id(get_xv6fs_client()->space_id, GPICAP_TYPE_FILE), tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Add file to FD table
  seL4_CPtr dest = seL4_GetMR(FSMSGREG_CREATE_ACK_DEST);
  int fd = fd_bind(dest);

  if (fd == -1)
  {
    XV6FS_PRINTF("Ran out of slots in the FD table");
    return -1;
  }

  XV6FS_PRINTF("Opened fd %d\n", fd);

  return fd;
}

static int xv6fs_libc_pread(int fd, void *buf, int count, int offset)
{
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
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_READ_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_READ_REQ);
  seL4_SetMR(FSMSGREG_READ_REQ_N, count);
  seL4_SetMR(FSMSGREG_READ_REQ_OFFSET, offset);
  seL4_SetCap(0, get_xv6fs_client()->shared_mem->badged_server_ep_cspath.capPtr);
  tag = seL4_Call(file->badged_server_ep_cspath.capPtr, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Copy from shared mem to buf
  int bytes_read = seL4_GetMR(FSMSGREG_READ_ACK_N);
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
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_WRITE_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_WRITE_REQ);
  seL4_SetMR(FSMSGREG_WRITE_REQ_N, count);
  seL4_SetMR(FSMSGREG_WRITE_REQ_OFFSET, file->offset);
  seL4_SetCap(0, get_xv6fs_client()->shared_mem->badged_server_ep_cspath.capPtr);
  tag = seL4_Call(file->badged_server_ep_cspath.capPtr, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Update file offset
  int bytes_written = seL4_GetMR(FSMSGREG_WRITE_ACK_N);
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
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, FS_FUNC_CLOSE_REQ);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_CLOSE_REQ);
  tag = seL4_Call(file->badged_server_ep_cspath.capPtr, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    XV6FS_PRINTF("Server failed to close file\n");
    error = -1;
  }

  // Close the FD locally
  fd_close(fd);

  return error;
}

static int xv6fs_libc_lseek(int fd, off_t offset, int whence)
{
  XV6FS_PRINTF("xv6fs_libc_lseek fd %d offset %ld whence %d\n", fd, offset, whence);

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

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("xv6fs_libc_fstat: Invalid FD provided\n");
    return -1;
  }

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_STAT_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_STAT_REQ);
  seL4_SetCap(0, get_xv6fs_client()->shared_mem->badged_server_ep_cspath.capPtr);
  tag = seL4_Call(file->badged_server_ep_cspath.capPtr, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
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
    errno = ENOENT;
    return -1;
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

  // Copy pathname from buf to shared mem
  strcpy(get_xv6fs_client()->shared_mem_vaddr, pathname);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_UNLINK_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_UNLINK_REQ);
  seL4_SetCap(0, get_xv6fs_client()->shared_mem->badged_server_ep_cspath.capPtr);
  tag = seL4_Call(sel4gpi_get_rde_by_space_id(get_xv6fs_client()->space_id, GPICAP_TYPE_FILE), tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
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

static void init_global_libc_fs_ops(void)
{
  libc_fs_ops.open = xv6fs_libc_open;
  libc_fs_ops.pread = xv6fs_libc_pread;
  libc_fs_ops.read = xv6fs_libc_read;
  libc_fs_ops.write = xv6fs_libc_write;
  libc_fs_ops.close = xv6fs_libc_close;
  libc_fs_ops.lseek = xv6fs_libc_lseek;
  libc_fs_ops.stat = xv6fs_libc_stat;
  libc_fs_ops.fstat = xv6fs_libc_fstat;
  libc_fs_ops.getcwd = xv6fs_libc_getcwd;
  libc_fs_ops.fcntl = xv6fs_libc_fcntl;
  libc_fs_ops.unlink = xv6fs_libc_unlink;
  libc_fs_ops.fcntl = xv6fs_libc_fcntl;
  libc_fs_ops.access = xv6fs_libc_access;
}