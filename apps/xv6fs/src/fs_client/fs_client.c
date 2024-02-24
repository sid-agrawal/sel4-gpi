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

#include <libc_fs_helpers.h>
#include <fs_shared.h>
#include <fs_client.h>

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


static xv6fs_client_context_t xv6fs_client;

xv6fs_client_context_t *get_xv6fs_client(void)
{
  return &xv6fs_client;
}

static void init_global_libc_fs_ops(void);

/**
 * Map a shared frame in the vspace
 */
static seL4_Error map_shared_frame(vspace_t *vspace, seL4_CPtr *frame, void **vaddr)
{
  *vaddr = vspace_map_pages(vspace, frame, NULL, seL4_ReadWrite, 1, seL4_PageBits, 0);

  return vaddr == NULL; // Returns error if vaddr is NULL
}

/**
 * Unmap a shared frame from the vspace
 */
static void unmap_shared_frame(vspace_t *vspace, void *vaddr)
{
  // ARYA-TODO free the shared mem cap as well
  vspace_unmap_pages(vspace, vaddr, 1, seL4_PageBits, NULL);
}

seL4_Error
xv6fs_client_init(vka_t *client_vka,
                  vspace_t *client_vspace,
                  seL4_CPtr server_ep_cap)
{
  get_xv6fs_client()->client_vka = client_vka;
  get_xv6fs_client()->client_vspace = client_vspace;
  get_xv6fs_client()->server_ep_cap = server_ep_cap;

  /* Alloc shared memory for IPC */
  seL4_Error error;
  vka_object_t frame_obj;
  error = vka_alloc_frame(get_xv6fs_client()->client_vka, seL4_PageBits, &frame_obj);
  CHECK_ERROR(error, "failed to allocate shared memory\n");

  error = map_shared_frame(get_xv6fs_client()->client_vspace, &frame_obj.cptr, &get_xv6fs_client()->shared_mem);
  CHECK_ERROR(error, "failed to map shared memory\n");
  // ARYA-TODO: support deregistering client and unmapping these frames

  /* Register shared mem with the server */
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
  seL4_SetMR(XV6FS_OP, XV6FS_REGISTER_CLIENT);
  seL4_SetCap(0, frame_obj.cptr);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);
  error = seL4_MessageInfo_get_label(tag);
  CHECK_ERROR(error, "failed to register client with xv6fs server\n");

  /* Override libc fs ops */
  init_global_libc_fs_ops();

  return error;
}

/* Remote fs access functions to override libc fs ops */
static int xv6fs_remote_open(const char *pathname, int flags, int modes)
{
  // Copy pathname to ipc frame
  memcpy(get_xv6fs_client()->shared_mem, pathname, strlen(pathname) + 1);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
  seL4_SetMR(XV6FS_OP, XV6FS_OPEN);
  seL4_SetMR(XV6FS_FLAGS, flags);
  seL4_SetMR(XV6FS_MODE, modes);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

static int xv6fs_remote_read(int fd, void *buf, int count)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
  seL4_SetMR(XV6FS_OP, XV6FS_READ);
  seL4_SetMR(XV6FS_FD, fd);
  seL4_SetMR(XV6FS_COUNT, count);
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

static int xv6fs_remote_write(int fd, const void *buf, int count)
{
  // Copy buf to ipc frame
  memcpy(get_xv6fs_client()->shared_mem, buf, count);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
  seL4_SetMR(XV6FS_OP, XV6FS_WRITE);
  seL4_SetMR(XV6FS_FD, fd);
  seL4_SetMR(XV6FS_COUNT, count);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError)
  {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

static int xv6fs_remote_fstat(int fd, struct stat *buf)
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

static int xv6fs_remote_stat(const char *pathname, struct stat *buf)
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

static int xv6fs_remote_lseek(int fd, off_t offset, int whence)
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

static int xv6fs_remote_close(int fd)
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

static int xv6fs_remote_unlink(const char *pathname)
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

static char *xv6fs_remote_getcwd(char *buf, size_t size)
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

static int xv6fs_remote_fcntl(int fd, int cmd, ...)
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
    memcpy(get_xv6fs_client()->shared_mem, (void *) arg, sizeof(struct flock));
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
    memcpy((void *) arg, get_xv6fs_client()->shared_mem, sizeof(struct flock));
    break;
  }

  return seL4_GetMR(XV6FS_RET);
}

static void init_global_libc_fs_ops(void)
{
  libc_fs_ops.open = xv6fs_remote_open;
  libc_fs_ops.read = xv6fs_remote_read;
  libc_fs_ops.write = xv6fs_remote_write;
  libc_fs_ops.stat = xv6fs_remote_stat;
  libc_fs_ops.fstat = xv6fs_remote_fstat;
  libc_fs_ops.lseek = xv6fs_remote_lseek;
  libc_fs_ops.close = xv6fs_remote_close;
  libc_fs_ops.unlink = xv6fs_remote_unlink;
  libc_fs_ops.getcwd = xv6fs_remote_getcwd;
  libc_fs_ops.fcntl = xv6fs_remote_fcntl;
  libc_fs_ops.pread = xv6fs_remote_pread;
}