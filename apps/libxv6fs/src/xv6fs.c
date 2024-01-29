/**
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vspace/vspace.h>
#include <libc_fs_helpers.h>

#include <xv6fs/xv6fs.h>
#include <xv6fs/defs.h>
#include <xv6fs/stat.h>
#include <xv6fs/spinlock.h>
#include <xv6fs/sleeplock.h>
#include <xv6fs/proc.h>
#include <xv6fs/fs.h>
#include <xv6fs/buf.h>
#include <xv6fs/file.h>

/* Memory regions for IPC to xv6fs server */
#define XV6FS_OP 0

// open
#define XV6FS_FLAGS 1
#define XV6FS_MODE 2

// read / write
#define XV6FS_FD 1
#define XV6FS_COUNT 2

// seek
#define XV6FS_OFFSET 2
#define XV6FS_WHENCE 3

// return values
#define XV6FS_RET 0

/* xv6fs opcodes */
#define XV6FS_REGISTER_CLIENT 0
#define XV6FS_OPEN 1
#define XV6FS_READ 2
#define XV6FS_WRITE 3
#define XV6FS_STAT 4
#define XV6FS_FSTAT 5
#define XV6FS_LSEEK 6
#define XV6FS_CLOSE 7
#define XV6FS_UNLINK 8

/* Other constants */
#define IPC_FRAME_PAGE_BITS seL4_PageBits

#define XV6FS_PRINTF(...)   \
  do                        \
  {                         \
    printf("%s ", XV6FS_S); \
    printf(__VA_ARGS__);    \
  } while (0);

#define CHECK_ERROR(error, msg) \
  do                            \
  {                             \
    if (error != seL4_NoError)  \
    {                           \
      ZF_LOGE(XV6FS_S "%s: %s"  \
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
      ZF_LOGE(XV6FS_S "%s: %s"       \
                      ", %d.",       \
              __func__,              \
              msg,                   \
              error);                \
      goto exit;                     \
    }                                \
  } while (0);

/*--- XV6FS SERVER ---*/
static xv6fs_server_context_t xv6fs_server;

xv6fs_server_context_t *get_xv6fs_server(void)
{
  return &xv6fs_server;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
  /** NOTE:

   * the reply param of api_recv(third param) is only used in the MCS kernel.
   **/

  return api_recv(get_xv6fs_server()->server_ep_obj.cptr,
                  sender_badge_ptr,
                  get_xv6fs_server()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
  api_reply(get_xv6fs_server()->server_thread.reply.cptr, tag);
}

seL4_Error
xv6fs_server_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                          vspace_t *parent_vspace,
                          int (*block_read)(uint, void *),
                          int (*block_write)(uint, void *),
                          uint8_t priority,
                          seL4_CPtr *server_ep_cap)
{
  seL4_Error error;
  cspacepath_t parent_cspace_cspath;
  seL4_MessageInfo_t tag;

  if (parent_simple == NULL || parent_vka == NULL || parent_vspace == NULL)
  {
    return seL4_InvalidArgument;
  }

  get_xv6fs_server()->server_simple = parent_simple;
  get_xv6fs_server()->server_vka = parent_vka;
  get_xv6fs_server()->server_cspace = parent_cspace_cspath.root;
  get_xv6fs_server()->server_vspace = parent_vspace;
  get_xv6fs_server()->block_read = block_read;
  get_xv6fs_server()->block_write = block_write;
  get_xv6fs_server()->shared_mem = NULL;

  /* Get a CPtr to the parent's root cnode. */
  vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

  /* Allocate the Endpoint that the server will be listening on. */
  error = vka_alloc_endpoint(parent_vka, &get_xv6fs_server()->server_ep_obj);
  if (error != seL4_NoError)
  {
    ZF_LOGE(XV6FS_S "spawn_thread: failed to alloc endpoint, err=%d.",
            error);
    return error;
  }

  *server_ep_cap = get_xv6fs_server()->server_ep_obj.cptr;

  // Initialize the fs
  error = init_disk_file();
  CHECK_ERROR(error, "failed to initialize disk file");
  binit();
  fileinit();
  fsinit(ROOTDEV);
  fd_init();

  XV6FS_PRINTF("initialized\n");

  /* Configure thread */
  sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                           parent_cspace_cspath.root,
                                                           seL4_NilData,
                                                           get_xv6fs_server()->server_ep_obj.cptr,
                                                           priority);

  error = sel4utils_configure_thread_config(parent_vka,
                                            parent_vspace,
                                            parent_vspace,
                                            config,
                                            &get_xv6fs_server()->server_thread);
  if (error != seL4_NoError)
  {
    ZF_LOGE(XV6FS_S "spawn_thread: sel4utils_configure_thread failed "
                    "with %d.",
            error);
    goto out;
  }

  NAME_THREAD(get_xv6fs_server()->server_thread.tcb.cptr, "xv6fs server");
  error = sel4utils_start_thread(&get_xv6fs_server()->server_thread,
                                 (sel4utils_thread_entry_fn)&xv6fs_server_main,
                                 NULL, NULL, 1);
  if (error != seL4_NoError)
  {
    ZF_LOGE(XV6FS_S "spawn_thread: sel4utils_start_thread failed with "
                    "%d.",
            error);
    goto out;
  }

  return 0;

out:
  XV6FS_PRINTF("spawn_thread: Server ran into an error.\n");
  vka_free_object(parent_vka, &get_xv6fs_server()->server_ep_obj); // ARYA-TODO does this unmap?
  return error;
}

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

/**
 * @brief The starting point for the xv6fs server's thread.
 *
 */
void xv6fs_server_main()
{
  XV6FS_PRINTF("started\n");

  seL4_MessageInfo_t tag;
  seL4_Error error = 0;
  seL4_Word sender_badge;
  cspacepath_t received_cap_path;

  // Allocate initial cap receive path
  error = vka_cspace_alloc_path(get_xv6fs_server()->server_vka, &received_cap_path);
  if (error != seL4_NoError)
  {
    ZF_LOGE(XV6FS_S "%s: failed to alloc initial cap receive path ",
            __func__);
    goto exit;
  }

  seL4_SetCapReceivePath(
      /* _service */ received_cap_path.root,
      /* index */ received_cap_path.capPtr,
      /* depth */ received_cap_path.capDepth);

  while (1)
  {
    tag = recv(&sender_badge);
    unsigned int op = seL4_GetMR(XV6FS_OP);

    int ret;
    switch (op)
    {
    case XV6FS_REGISTER_CLIENT:
      // Map the shared memory page
      assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
      map_shared_frame(get_xv6fs_server()->server_vspace, &received_cap_path.capPtr, &get_xv6fs_server()->shared_mem);

      /* Alloc slot for future IPC caps */
      // ARYA-TODO need to free this slot?
      error = vka_cspace_alloc_path(get_xv6fs_server()->server_vka, &received_cap_path);
      assert(error == 0);

      seL4_SetCapReceivePath(
          /* _service */ received_cap_path.root,
          /* index */ received_cap_path.capPtr,
          /* depth */ received_cap_path.capDepth);

      break;
    case XV6FS_OPEN:
      const char *pathname = get_xv6fs_server()->shared_mem;
      ret = xv6fs_open(pathname, seL4_GetMR(XV6FS_FLAGS), seL4_GetMR(XV6FS_MODE));
      break;
    case XV6FS_READ:
      void *readbuf = get_xv6fs_server()->shared_mem;
      ret = xv6fs_read(seL4_GetMR(XV6FS_FD), readbuf, seL4_GetMR(XV6FS_COUNT));
      break;
    case XV6FS_WRITE:
      const void *writebuf = get_xv6fs_server()->shared_mem;
      ret = xv6fs_write(seL4_GetMR(XV6FS_FD), writebuf, seL4_GetMR(XV6FS_COUNT));
      break;
    case XV6FS_STAT:
      pathname = get_xv6fs_server()->shared_mem;
      struct stat* statbuf = get_xv6fs_server()->shared_mem;
      ret = xv6fs_stat(pathname, statbuf);
      break;
    case XV6FS_FSTAT:
      statbuf = get_xv6fs_server()->shared_mem;
      ret = xv6fs_fstat(seL4_GetMR(XV6FS_FD), statbuf);
      break;
    case XV6FS_LSEEK:
      ret = xv6fs_lseek(seL4_GetMR(XV6FS_FD), seL4_GetMR(XV6FS_OFFSET), seL4_GetMR(XV6FS_WHENCE));
      break;
    case XV6FS_CLOSE:
      ret = xv6fs_close(seL4_GetMR(XV6FS_FD));
      break;
    case XV6FS_UNLINK:
      pathname = get_xv6fs_server()->shared_mem;
      ret = xv6fs_unlink(pathname);
      break;
    default:
      ZF_LOGE(XV6FS_S "%s: got unexpected opcode %d\n",
              __func__,
              op);

      error = 1;
    }

    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(error, 0, 0, 1);
    seL4_SetMR(XV6FS_RET, ret);
    reply(reply_tag);
  }

  // serial_server_func_kill();
  /* After we break out of the loop, seL4_TCB_Suspend ourselves */
exit:
  ZF_LOGI(XV6FS_S "main: Suspending.");
  unmap_shared_frame(get_xv6fs_server()->server_vspace, get_xv6fs_server()->shared_mem);
  seL4_TCB_Suspend(get_xv6fs_server()->server_thread.tcb.cptr);
}

/*--- XV6FS CLIENT ---*/

static xv6fs_client_context_t xv6fs_client;
void init_global_libc_fs_ops(void);

xv6fs_client_context_t *get_xv6fs_client(void)
{
  return &xv6fs_client;
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
int xv6fs_remote_open(const char *pathname, int flags, int modes)
{
  // Copy pathname to ipc frame
  memcpy(get_xv6fs_client()->shared_mem, pathname, strlen(pathname) + 1);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
  seL4_SetMR(XV6FS_OP, XV6FS_OPEN);
  seL4_SetMR(XV6FS_FLAGS, flags);
  seL4_SetMR(XV6FS_MODE, modes);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);
  
  if (seL4_MessageInfo_get_label(tag) != seL4_NoError) {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

int xv6fs_remote_read(int fd, void *buf, int count)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
  seL4_SetMR(XV6FS_OP, XV6FS_READ);
  seL4_SetMR(XV6FS_FD, fd);
  seL4_SetMR(XV6FS_COUNT, count);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError) {
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

int xv6fs_remote_write(int fd, const void *buf, int count)
{
  // Copy buf to ipc frame
  memcpy(get_xv6fs_client()->shared_mem, buf, count);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
  seL4_SetMR(XV6FS_OP, XV6FS_WRITE);
  seL4_SetMR(XV6FS_FD, fd);
  seL4_SetMR(XV6FS_COUNT, count);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError) {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

int xv6fs_remote_fstat(int fd, struct stat *buf)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
  seL4_SetMR(XV6FS_OP, XV6FS_FSTAT);
  seL4_SetMR(XV6FS_FD, fd);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError) {
    return -1;
  }

  // Copy ipc frame to buf
  int res = seL4_GetMR(XV6FS_RET);
  if (res > 0)
  {
    memcpy(buf, get_xv6fs_client()->shared_mem, sizeof(struct stat));
  }

  return res;
}

int xv6fs_remote_stat(const char *pathname, struct stat *buf)
{
  // Copy pathname to ipc frame
  memcpy(get_xv6fs_client()->shared_mem, pathname, strlen(pathname) + 1);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
  seL4_SetMR(XV6FS_OP, XV6FS_STAT);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);
  
  if (seL4_MessageInfo_get_label(tag) != seL4_NoError) {
    return -1;
  }

  // Copy ipc frame to buf
  int res = seL4_GetMR(XV6FS_RET);
  if (res > 0)
  {
    memcpy(buf, get_xv6fs_client()->shared_mem, sizeof(struct stat));
  }

  return res;
}

int xv6fs_remote_lseek(int fd, off_t offset, int whence)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
  seL4_SetMR(XV6FS_OP, XV6FS_LSEEK);
  seL4_SetMR(XV6FS_FD, fd);
  seL4_SetMR(XV6FS_OFFSET, offset);
  seL4_SetMR(XV6FS_WHENCE, whence);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError) {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

int xv6fs_remote_close(int fd)
{
  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
  seL4_SetMR(XV6FS_OP, XV6FS_CLOSE);
  seL4_SetMR(XV6FS_FD, fd);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);

  if (seL4_MessageInfo_get_label(tag) != seL4_NoError) {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

int xv6fs_remote_unlink(const char *pathname)
{
  // Copy pathname to ipc frame
  memcpy(get_xv6fs_client()->shared_mem, pathname, strlen(pathname) + 1);

  // Send IPC to fs server
  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
  seL4_SetMR(XV6FS_OP, XV6FS_UNLINK);
  tag = seL4_Call(get_xv6fs_client()->server_ep_cap, tag);
  
  if (seL4_MessageInfo_get_label(tag) != seL4_NoError) {
    return -1;
  }

  return seL4_GetMR(XV6FS_RET);
}

void init_global_libc_fs_ops(void)
{
  libc_fs_ops.open = xv6fs_remote_open;
  libc_fs_ops.read = xv6fs_remote_read;
  libc_fs_ops.write = xv6fs_remote_write;
  libc_fs_ops.stat = xv6fs_remote_stat;
  libc_fs_ops.fstat = xv6fs_remote_fstat;
  libc_fs_ops.lseek = xv6fs_remote_lseek;
  libc_fs_ops.close = xv6fs_remote_close;
  libc_fs_ops.unlink = xv6fs_remote_unlink;
}

/* Override xv6 block read/write functions */
void xv6fs_bread(uint sec, void *buf)
{
  get_xv6fs_server()->block_read(sec, buf);
}

void xv6fs_bwrite(uint sec, void *buf)
{
  get_xv6fs_server()->block_write(sec, buf);
}

void disk_rw(struct buf *b, int write)
{
  if (write)
  {
    xv6fs_bwrite(b->blockno, b->data);
  }
  else
  {
    xv6fs_bread(b->blockno, b->data);
  }
}
