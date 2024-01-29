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

/* xv6fs opcodes */
#define XV6FS_REGISTER_CLIENT 0

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

static void test_fs(void) {
  char *test_string = "a little hedgehog";
  char buf[128];

  // Write test string to file
  XV6FS_PRINTF("Opening file\n");

  int fd = xv6fs_open("test-file.txt", O_CREATE | O_RDWR, 0);
  XV6FS_PRINTF("Opened file\n");

  int nwrite = xv6fs_write(fd, test_string, strlen(test_string) + 1);
  assert(nwrite == strlen(test_string) + 1);

  XV6FS_PRINTF("Wrote string to file: %s\n", test_string);

  // Seek beginning of file and read back
  int error = xv6fs_lseek(fd, 0, 0);
  assert(error == 0);
  XV6FS_PRINTF("Seeked file offset 0\n");

  int nread = xv6fs_read(fd, buf, 128);
  assert(nread == strlen(test_string) + 1);

  XV6FS_PRINTF("Read string from file: %s\n", buf);

  error = xv6fs_close(fd);
  assert(error == 0);
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

  test_fs();

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

    seL4_MessageInfo_t reply_tag;

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
    default:
      ZF_LOGE(XV6FS_S "%s: got unexpected opcode %d\n",
              __func__,
              op);

      error = 1;
    }

  done:
    reply_tag = seL4_MessageInfo_new(error, 0, 0, 1);
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

  return error;
}

/* Custom implementations of functions for xv6 */
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
