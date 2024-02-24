/**
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vspace/vspace.h>
#include <libc_fs_helpers.h>

#include <fs_server.h>
#include <defs.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <proc.h>
#include <fs.h>
#include <buf.h>
#include <file.h>

#include <fs_shared.h>

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

static int block_read(uint blockno, void *buf) {
  // (XXX) Arya: implement to call ramdisk server
  return 0;
}

static int block_write(uint blockno, void *buf) {
  // (XXX) Arya: implement to call ramdisk server
  return 0;
}

seL4_Error
xv6fs_server_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                          vspace_t *parent_vspace,
                          seL4_CPtr gpi_ep,
                          seL4_CPtr rd_ep,
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
  get_xv6fs_server()->gpi_ep = gpi_ep;
  get_xv6fs_server()->rd_ep = rd_ep;
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

  /* Initialize the fs */
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
      struct stat *statbuf = get_xv6fs_server()->shared_mem;
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
    case XV6FS_GETCWD:
      char *buf = get_xv6fs_server()->shared_mem;
      char *getcwd_ret = xv6fs_getcwd(buf, seL4_GetMR(XV6FS_SIZE));
      ret = (getcwd_ret != NULL);
      break;
    case XV6FS_FCNTL:
      int cmd = seL4_GetMR(XV6FS_CMD);
      unsigned long arg = seL4_GetMR(XV6FS_ARG);
      switch (cmd)
      {
      case F_SETLK:
      case F_SETLKW:
      case F_GETLK:
        arg = (unsigned long)get_xv6fs_server()->shared_mem;
        break;
      }

      ret = xv6fs_fcntl(seL4_GetMR(XV6FS_FD), cmd, arg);
      break;
    case XV6FS_PREAD:
      readbuf = get_xv6fs_server()->shared_mem;
      ret = xv6fs_pread(seL4_GetMR(XV6FS_FD), readbuf, seL4_GetMR(XV6FS_COUNT), seL4_GetMR(XV6FS_POFFSET));
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
