#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/pd_clientapi.h>

#include <libc_fs_helpers.h>
#include <fs_shared.h>
#include <fs_client.h>

#define FD_TABLE_SIZE 32
#define FS_APP "fs_server"

#define XV6FS_PRINTF(...)   \
  do                        \
  {                         \
    printf("%s ", XV6FS_C); \
    printf(__VA_ARGS__);    \
  } while (0);

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
}

// Add a file to the file descriptor table
int fd_bind(seL4_CPtr file_cap)
{
  for (int i = 1; i < FD_TABLE_SIZE; i++)
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
    return &fd_table[fd];
  }
  return NULL;
}

void fd_close(int fd)
{
  if (fd >= 0 && fd < FD_TABLE_SIZE)
  {
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

static int vka_next_slot_fn(seL4_CPtr *slot)
{
  return vka_cspace_alloc(get_xv6fs_client()->client_vka, slot);
}

int start_xv6fs_pd(vka_t *vka,
                   seL4_CPtr gpi_ep,
                   seL4_CPtr rd_ep,
                   seL4_CPtr *fs_ep)
{
  int error;

  // Create an endpoint for the parent to listen on
  vka_object_t ep_object = {0};
  error = vka_alloc_endpoint(vka, &ep_object);
  CHECK_ERROR(error, "failed to allocate endpoint");

  // Create a new PD
  pd_client_context_t pd_os_cap;
  error = pd_component_client_connect(gpi_ep, vka, &pd_os_cap);
  CHECK_ERROR(error, "failed to create new pd");

  // Create a new ADS Cap, which will be in the context of a PD and image
  ads_client_context_t ads_os_cap;
  error = ads_component_client_connect(gpi_ep, vka, &ads_os_cap);
  CHECK_ERROR(error, "failed to create new ads");

  // Make a new AS, loads an image
  error = pd_client_load(&pd_os_cap, &ads_os_cap, FS_APP);
  CHECK_ERROR(error, "failed to load pd image");

  // Copy the parent ep to the new PD
  seL4_Word parent_ep_slot;
  error = pd_client_send_cap(&pd_os_cap, ep_object.cptr, &parent_ep_slot);
  CHECK_ERROR(error, "failed to send parent's ep cap to pd");

  // Copy the ramdisk ep to the new PD
  // (XXX) Arya: replace with RDE mechanism once implemented
  seL4_Word ramdisk_ep_slot;
  error = pd_client_send_cap(&pd_os_cap, rd_ep, &ramdisk_ep_slot);
  CHECK_ERROR(error, "failed to send ramdisk's ep cap to pd");
  assert(ramdisk_ep_slot == parent_ep_slot - 1);

  // Start it
  error = pd_client_start(&pd_os_cap, parent_ep_slot); // with this arg.
  CHECK_ERROR(error, "failed to start pd");

  // Wait for it to finish starting
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

  /* Alloc cap receive path*/
  cspacepath_t received_cap_path;
  error = vka_cspace_alloc_path(vka, &received_cap_path);
  CHECK_ERROR(error, "failed to alloc receive endpoint");

  seL4_SetCapReceivePath(received_cap_path.root,
                         received_cap_path.capPtr,
                         received_cap_path.capDepth);

  tag = seL4_Recv(ep_object.cptr, NULL);
  int n_caps = seL4_MessageInfo_get_extraCaps(tag);
  CHECK_ERROR(n_caps != 1, "message from ramdisk does not contain ep");
  *fs_ep = received_cap_path.capPtr;

  XV6FS_PRINTF("Successfully started ramdisk server\n");
  return 0;
}

seL4_Error
xv6fs_client_init(vka_t *client_vka,
                  seL4_CPtr fs_ep,
                  seL4_CPtr gpi_ep,
                  seL4_CPtr ads_ep,
                  seL4_CPtr pd_ep)
{
  XV6FS_PRINTF("Initializing client of FS server\n");

  int error;

  get_xv6fs_client()->client_vka = client_vka;
  get_xv6fs_client()->fs_ep = fs_ep;
  get_xv6fs_client()->gpi_ep = gpi_ep;
  get_xv6fs_client()->ads_conn = malloc(sizeof(ads_client_context_t));
  get_xv6fs_client()->ads_conn->badged_server_ep_cspath.capPtr = ads_ep;
  get_xv6fs_client()->pd_conn = malloc(sizeof(pd_client_context_t));
  get_xv6fs_client()->pd_conn->badged_server_ep_cspath.capPtr = pd_ep;
  get_xv6fs_client()->next_slot = vka_next_slot_fn;

  /* Allocate the TEMP shared memory object */
  get_xv6fs_client()->shared_mem = malloc(sizeof(mo_client_context_t));
  seL4_CPtr free_slot;
  error = get_xv6fs_client()->next_slot(&free_slot);
  CHECK_ERROR(error, "failed to get next cspace slot");

  error = mo_component_client_connect(get_xv6fs_client()->gpi_ep,
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

/* Remote fs access functions to override libc fs ops */
static int xv6fs_libc_open(const char *pathname, int flags, int modes)
{
  XV6FS_PRINTF("xv6fs_libc_open\n");

  int error;
  // (XXX) Arya: How to send pathname?
  // memcpy(get_xv6fs_client()->shared_mem, pathname, strlen(pathname) + 1);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, FSMSGREG_CREATE_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_CREATE_REQ);
  // (XXX) Currently ignore flags and modes

  // Alloc received cap ep
  cspacepath_t path;
  error = vka_cspace_alloc_path(get_xv6fs_client()->client_vka, &path);
  CHECK_ERROR(error, "failed to alloc slot");
  seL4_SetCapReceivePath(path.root, path.capPtr, path.capDepth);
  tag = seL4_Call(get_xv6fs_client()->fs_ep, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Add file to FD table
  int fd = fd_bind(path.capPtr);

  if (fd == -1)
  {
    XV6FS_PRINTF("Ran out of slots in the FD table");
    return -1;
  }

  XV6FS_PRINTF("Opened fd %d\n", fd);

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("Invalid FD provided");
    return -1;
  }

  printf("File %p offset is %ld\n", file, file->offset);

  return fd;
}

static int xv6fs_libc_read(int fd, void *buf, int count)
{
  XV6FS_PRINTF("xv6fs_libc_read fd %d len %d\n", fd, count);

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("Invalid FD provided");
    return -1;
  }

  printf("File %p offset is %ld\n", file, file->offset);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, FSMSGREG_READ_REQ_END);
  seL4_SetMR(FSMSGREG_FUNC, FS_FUNC_READ_REQ);
  seL4_SetMR(FSMSGREG_READ_REQ_N, count);
  seL4_SetMR(FSMSGREG_READ_REQ_OFFSET, file->offset);
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
    file->offset += bytes_read;
  }

  return bytes_read;
}

static int xv6fs_libc_write(int fd, const void *buf, int count)
{
  XV6FS_PRINTF("xv6fs_libc_write fd %d len %d\n", fd, count);

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("Invalid FD provided");
    return -1;
  }

  // Copy from shared buf to shared mem
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

  // Copy from shared buf to shared mem
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

  // (XXX) Arya: Do we need the server to do anything when we close fd?

  // Close the FD
  fd_close(fd);

  return 0;
}

static int xv6fs_libc_lseek(int fd, off_t offset, int whence)
{
  XV6FS_PRINTF("xv6fs_libc_lseek fd %d offset %ld whence %d\n", fd, offset, whence);

  // Handle file offset locally

  // Find the file by fd
  xv6fs_client_context_t *file = fd_get(fd);
  if (file == NULL)
  {
    XV6FS_PRINTF("Invalid FD provided");
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

#if 0
static int xv6fs_remote_pread(int fd, void *buf, int count, int offset)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
  seL4_SetMR(XV6FS_OP, XV6FS_PREAD);
  seL4_SetMR(XV6FS_FD, fd);
  seL4_SetMR(XV6FS_COUNT, count);
  seL4_SetMR(XV6FS_POFFSET, offset);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Copy ipc frame to buf
  int res = seL4_GetMR(XV6FS_RET);
  if (res > 0)
  {
    memcpy(buf, get_xv6fs_client()->shared_mem, res);
  }

  return res;
}

static int xv6fs_libc_fstat(int fd, struct stat *buf)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
  seL4_SetMR(XV6FS_OP, XV6FS_FSTAT);
  seL4_SetMR(XV6FS_FD, fd);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Copy ipc frame to buf
  int res = seL4_GetMR(XV6FS_RET);
  if (res != -1)
  {
    memcpy(buf, get_xv6fs_client()->shared_mem, sizeof(struct stat));
  }

  return res;
}

static int xv6fs_libc_stat(const char *pathname, struct stat *buf)
{
  // Copy pathname to ipc frame
  memcpy(get_xv6fs_client()->shared_mem, pathname, strlen(pathname) + 1);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
  seL4_SetMR(XV6FS_OP, XV6FS_STAT);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Copy ipc frame to buf
  int res = seL4_GetMR(XV6FS_RET);
  if (res != -1)
  {
    memcpy(buf, get_xv6fs_client()->shared_mem, sizeof(struct stat));
  }

  return res;
}

static int xv6fs_libc_lseek(int fd, off_t offset, int whence)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
  seL4_SetMR(XV6FS_OP, XV6FS_LSEEK);
  seL4_SetMR(XV6FS_FD, fd);
  seL4_SetMR(XV6FS_OFFSET, offset);
  seL4_SetMR(XV6FS_WHENCE, whence);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

static int xv6fs_libc_close(int fd)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
  seL4_SetMR(XV6FS_OP, XV6FS_CLOSE);
  seL4_SetMR(XV6FS_FD, fd);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

static int xv6fs_libc_unlink(const char *pathname)
{
  // Copy pathname to ipc frame
  memcpy(get_xv6fs_client()->shared_mem, pathname, strlen(pathname) + 1);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
  seL4_SetMR(XV6FS_OP, XV6FS_UNLINK);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

static char *xv6fs_libc_getcwd(char *buf, size_t size)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
  seL4_SetMR(XV6FS_OP, XV6FS_GETCWD);
  seL4_SetMR(XV6FS_SIZE, size);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return NULL;
  }

  // Copy pathname to ipc frame
  int ret = seL4_GetMR(XV6FS_RET);
  if (ret == 0)
  {
    return NULL;
  }

  memcpy(buf, get_xv6fs_client()->shared_mem, strlen(get_xv6fs_client()->shared_mem) + 1);

  return buf;
}

static int xv6fs_libc_fcntl(int fd, int cmd, ...)
{
  unsigned long arg;
  va_list ap;
  va_start(ap, cmd);
  arg = va_arg(ap, unsigned long);
  va_end(ap);

  // Copy arg if necessary
  switch (cmd)
  {
  case F_SETLK:
  case F_SETLKW:
  case F_GETLK:
    memcpy(get_xv6fs_client()->shared_mem, (void *)arg, sizeof(struct flock));
    break;
  }

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
  seL4_SetMR(XV6FS_OP, XV6FS_FCNTL);
  seL4_SetMR(XV6FS_FD, fd);
  seL4_SetMR(XV6FS_CMD, cmd);
  seL4_SetMR(XV6FS_ARG, arg);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  // Copy arg if necessary
  switch (cmd)
  {
  case F_GETLK:
    memcpy((void *)arg, get_xv6fs_client()->shared_mem, sizeof(struct flock));
    break;
  }

  return seL4_GetMR(XV6FS_RET);
}
#endif

static void init_global_libc_fs_ops(void)
{
  libc_fs_ops.open = xv6fs_libc_open;
  libc_fs_ops.read = xv6fs_libc_read;
  libc_fs_ops.write = xv6fs_libc_write;
  libc_fs_ops.close = xv6fs_libc_close;
  libc_fs_ops.lseek = xv6fs_libc_lseek;
#if 0
  libc_fs_ops.stat = xv6fs_libc_stat;
  libc_fs_ops.fstat = xv6fs_libc_fstat;
  libc_fs_ops.lseek = xv6fs_libc_lseek;
  libc_fs_ops.unlink = xv6fs_libc_unlink;
  libc_fs_ops.getcwd = xv6fs_libc_getcwd;
  libc_fs_ops.fcntl = xv6fs_libc_fcntl;
  libc_fs_ops.pread = xv6fs_libc_pread;
#endif
}