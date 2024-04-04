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
#include <sel4gpi/resource_server_utils.h>
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

/* Filepath resource DS */
typedef struct _filepath_resource_entry
{
  char path[FS_CLIENT_PATH_MAX];
  seL4_CPtr cap;
  struct _filepath_resource_entry *next;
} filepath_resource_entry_t;

filepath_resource_entry_t *filepaths;

static void insert_filepath_resource(char *path, seL4_CPtr cap)
{
  filepath_resource_entry_t *entry = malloc(sizeof(filepath_resource_entry_t));
  entry->cap = cap;
  strncpy(entry->path, path, FS_CLIENT_PATH_MAX);
  entry->next = filepaths;
  filepaths = entry;
}

static seL4_CPtr get_filepath_resource(char *path)
{
  filepath_resource_entry_t *curr;

  for (filepath_resource_entry_t *curr = filepaths; curr != NULL; curr = curr->next)
  {
    if (strcmp(path, curr->path) == 0)
    {
      return curr->cap;
    }
  }

  return 0;
}

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

static global_xv6fs_client_context_t xv6fs_client;

global_xv6fs_client_context_t *get_xv6fs_client(void)
{
  return &xv6fs_client;
}

int start_xv6fs_pd(uint64_t rd_id,
                   seL4_CPtr rd_pd_cap,
                   seL4_CPtr *fs_pd_cap,
                   uint64_t *file_manager_id,
                   uint64_t *path_manager_id)
{
  uint64_t manager_ids[2];
  int error = start_resource_server_pd(rd_id, rd_pd_cap,
                                       FS_APP, fs_pd_cap,
                                       2, manager_ids);
  CHECK_ERROR(error, "failed to start file resource server\n");

  if (file_manager_id)
  {
    *file_manager_id = manager_ids[0];
  }
  if (path_manager_id)
  {
    *path_manager_id = manager_ids[1];
  }

  XV6FS_PRINTF("Successfully started file system server\n");
  return 0;
}

seL4_Error
xv6fs_client_init(void)
{
  XV6FS_PRINTF("Initializing client of FS server\n");

  int error;

  get_xv6fs_client()->file_ep = sel4gpi_get_rde(GPICAP_TYPE_FILE);
  get_xv6fs_client()->filepath_ep = sel4gpi_get_rde(GPICAP_TYPE_FILEPATH);
  get_xv6fs_client()->mo_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);
  get_xv6fs_client()->ads_conn = malloc(sizeof(ads_client_context_t));
  get_xv6fs_client()->ads_conn->badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_ADS);
  get_xv6fs_client()->pd_conn = malloc(sizeof(pd_client_context_t));
  get_xv6fs_client()->pd_conn->badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

  /* Allocate the TEMP shared memory object */
  get_xv6fs_client()->shared_mem = malloc(sizeof(mo_client_context_t));
  seL4_CPtr free_slot;
  error = pd_client_next_slot(get_xv6fs_client()->pd_conn, &free_slot);
  CHECK_ERROR(error, "failed to get next cspace slot");

  error = mo_component_client_connect(get_xv6fs_client()->mo_ep,
                                      free_slot,
                                      1,
                                      get_xv6fs_client()->shared_mem);
  CHECK_ERROR(error, "failed to allocate shared mem page");

  error = ads_client_attach(get_xv6fs_client()->ads_conn,
                            NULL,
                            get_xv6fs_client()->shared_mem,
                            &get_xv6fs_client()->shared_mem_vaddr);
  CHECK_ERROR(error, "failed to map shared mem page");

  /* Setup local FD data structure */
  fd_init();

  /* Override libc fs ops */
  init_global_libc_fs_ops();

  return error;
}

int xv6fs_client_set_namespace(uint64_t ns_id)
{
  XV6FS_PRINTF("Client of FS server will use filepath namespace %ld\n", ns_id);

  seL4_CPtr ep = sel4gpi_get_rde_by_ns_id(ns_id, GPICAP_TYPE_FILEPATH);

  if (ep == seL4_CapNull)
  {
    return -1;
  }

  get_xv6fs_client()->filepath_ep = ep;
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
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_LINK_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_LINK_REQ);
  seL4_SetCap(0, file);

  tag = seL4_Call(get_xv6fs_client()->filepath_ep, tag);

  return seL4_MessageInfo_get_label(tag);
}

/**
 * Allocate a path within the file system, does not allocate a file
 **/
static int alloc_path(const char *pathname, seL4_CPtr *dest)
{
  // Copy pathname from buf to shared mem
  strcpy(get_xv6fs_client()->shared_mem_vaddr, pathname);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_CREATE_PATH_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_CREATE_PATH_REQ);
  seL4_SetCap(0, get_xv6fs_client()->shared_mem->badged_server_ep_cspath.capPtr);
  tag = seL4_Call(get_xv6fs_client()->filepath_ep, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Successfully allocated path
  *dest = seL4_GetMR(FSMSGREG_CREATE_FILE_ACK_DEST);
  insert_filepath_resource(pathname, *dest);

  return 0;
}

/**
 * Checks if a path in the file system is already linked to a file
 **/
static int is_path_linked(seL4_CPtr path_cap)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, FSMSGREG_IS_LINKED_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_IS_LINKED_REQ);
  tag = seL4_Call(path_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Successfully allocated path
  return seL4_GetMR(FSMSGREG_IS_LINKED_ACK_RES);
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

  // Check if we have this path resource already
  seL4_CPtr path_res = get_filepath_resource(pathname);

  if (path_res == 0)
  {
    if (flags & O_CREAT)
    {
      XV6FS_PRINTF("Don't already have path resource, try to allocate\n");

      // Allocate the path
      error = alloc_path(pathname, &path_res);
      CHECK_ERROR(error, "Failed to allocate path");

      // (XXX) Arya: Assumption is that only when we allocate the path do we need to allocate
      // the file as well. This will be true when using the file server through libc functions

      // Allocate & link the file
      seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_CREATE_FILE_REQ_END);
      seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_CREATE_FILE_REQ);
      seL4_SetMR(FSMSGREG_CREATE_FILE_REQ_FLAGS, flags);
      seL4_SetCap(0, path_res);
      // (XXX) Currently ignore modes
      tag = seL4_Call(get_xv6fs_client()->file_ep, tag);

      if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
      {
        return -1;
      }

      // (XXX) Arya: Ignoring the file resource for now, all operations go through the filepath
      // seL4_CPtr dest = seL4_GetMR(FSMSGREG_CREATE_FILE_ACK_DEST);
    }
    else
    {
      XV6FS_PRINTF("Don't already have path resource, no O_CREAT, failure\n");

      // The filepath doesn't exist yet and mode is not create
      errno = ENOENT;
      return -1;
    }
  } else {
    XV6FS_PRINTF("Already have path resource\n");
  }

  // Add file to FD table
  int fd = fd_bind(path_res);

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
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FS_FUNC_CLOSE_REQ);
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
  tag = seL4_Call(get_xv6fs_client()->filepath_ep, tag);

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