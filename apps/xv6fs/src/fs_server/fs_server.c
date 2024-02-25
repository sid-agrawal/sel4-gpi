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

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <ramdisk_client.h>

#include <libc_fs_helpers.h>
#include <fs_shared.h>
#include <fs_server.h>
#include <defs.h>
#include <file.h>
#include <buf.h>

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

#define CHECK_ERROR_GOTO(error, msg, dest) \
  do                                       \
  {                                        \
    if (error != seL4_NoError)             \
    {                                      \
      ZF_LOGE(XV6FS_S "%s: %s"             \
                      ", %d.",             \
              __func__,                    \
              msg,                         \
              error);                      \
      goto dest;                           \
    }                                      \
  } while (0);

/**
 * @brief Insert a new client into the client registry Linked List.
 *
 * @param new_node
 */
static void fs_registry_insert(fs_registry_entry_t *new_node)
{
  fs_registry_entry_t *head = get_xv6fs_server()->client_registry;

  if (head == NULL)
  {
    get_xv6fs_server()->client_registry = new_node;
    new_node->next = NULL;
    return;
  }

  while (head->next != NULL)
  {
    head = head->next;
  }
  head->next = new_node;
  new_node->next = NULL;
}

/**
 * @brief Lookup the client registry entry for the given id.
 *
 * @param objectID
 * @return fs_registry_entry_t*
 */
static fs_registry_entry_t *fs_registry_get_entry_by_id(uint64_t objectID)
{
  fs_registry_entry_t *current_ctx = get_xv6fs_server()->client_registry;

  while (current_ctx != NULL)
  {
    if ((seL4_Word)current_ctx->file->id == objectID)
    {
      break;
    }
    current_ctx = current_ctx->next;
  }
  return current_ctx;
}

/**
 * @brief Lookup the client registry entry for the given badge.
 *
 * @param badge
 * @return fs_registry_entry_t*
 */
static fs_registry_entry_t *fs_registry_get_entry_by_badge(seL4_Word badge)
{

  uint64_t objectID = get_object_id_from_badge(badge);
  return fs_registry_get_entry_by_id(objectID);
}

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

  return api_recv(get_xv6fs_server()->server_ep,
                  sender_badge_ptr,
                  get_xv6fs_server()->mcs_reply);
}

static inline void reply(seL4_MessageInfo_t tag)
{
  api_reply(get_xv6fs_server()->mcs_reply, tag);
}

static int vka_next_slot_fn(seL4_CPtr *slot)
{
  return vka_cspace_alloc(get_xv6fs_server()->server_vka, slot);
}

static int pd_next_slot_fn(seL4_CPtr *slot)
{
  return pd_client_next_slot(get_xv6fs_server()->pd_conn, slot);
}

static int vka_badge_ep_fn(seL4_Word badge, seL4_CPtr *badged_ep)
{
  cspacepath_t src, dest;
  vka_cspace_make_path(get_xv6fs_server()->server_vka,
                       get_xv6fs_server()->server_ep, &src);
  int error = vka_cspace_alloc_path(get_xv6fs_server()->server_vka,
                                    &dest);
  if (error)
  {
    return error;
  }

  error = vka_cnode_mint(&dest,
                         &src,
                         seL4_AllRights,
                         badge);

  *badged_ep = dest.capPtr;
  return error;
}

static int pd_badge_ep_fn(seL4_Word badge, seL4_CPtr *badged_ep)
{
  return pd_client_badge_ep(get_xv6fs_server()->pd_conn,
                            get_xv6fs_server()->server_ep,
                            badge, badged_ep);
}

/**
 * Temporary function initializes the file system by requesting
 * every block ahead of time, and using them later for read/write requests
 */
static int init_naive_blocks()
{
  int error;
  seL4_CPtr ramdisk_ep = get_xv6fs_server()->rd_ep;

  for (int i = 0; i < FS_SIZE; i++)
  {
    seL4_CPtr free_slot;
    error = get_xv6fs_server()->next_slot(&free_slot);
    CHECK_ERROR(error, "failed to get a free slot");
    error = ramdisk_client_alloc_block(ramdisk_ep, NULL, free_slot, &get_xv6fs_server()->naive_blocks[i]);
    CHECK_ERROR(error, "failed to alloc a block from ramdisk");
  }

  return 0;
}

int xv6fs_server_start(ads_client_context_t *ads_conn,
                       pd_client_context_t *pd_conn,
                       seL4_CPtr gpi_ep,
                       seL4_CPtr rd_ep,
                       seL4_CPtr parent_ep)
{
  seL4_Error error;

  xv6fs_server_context_t *server = get_xv6fs_server();

  server->gpi_ep = gpi_ep;
  server->rd_ep = rd_ep;
  server->ads_conn = ads_conn;
  server->pd_conn = pd_conn;
  server->parent_ep = parent_ep;
  server->next_slot = pd_next_slot_fn;
  server->badge_ep = pd_badge_ep_fn;

  /* Allocate the Endpoint that the server will be listening on. */
  error = pd_client_alloc_ep(server->pd_conn, &server->server_ep);
  CHECK_ERROR(error, "Failed to allocate endpoint for fs server");
  XV6FS_PRINTF("Allocated server ep at %d\n", (int)server->server_ep);

  XV6FS_PRINTF("Going to main function\n");
  return xv6fs_server_main();
}

seL4_Error
xv6fs_server_spawn_thread(simple_t *parent_simple,
                          vka_t *parent_vka,
                          vspace_t *parent_vspace,
                          seL4_CPtr gpi_ep,
                          seL4_CPtr rd_ep,
                          seL4_CPtr parent_ep,
                          seL4_CPtr ads_ep,
                          seL4_CPtr pd_ep,
                          uint8_t priority)
{
  seL4_Error error;
  cspacepath_t parent_cspace_cspath;
  seL4_MessageInfo_t tag;

  if (parent_simple == NULL || parent_vka == NULL || parent_vspace == NULL)
  {
    return seL4_InvalidArgument;
  }

  xv6fs_server_context_t *server = get_xv6fs_server();

  server->server_vka = parent_vka;
  server->gpi_ep = gpi_ep;
  server->parent_ep = parent_ep;
  server->rd_ep = rd_ep;
  server->ads_conn = malloc(sizeof(ads_client_context_t));
  server->ads_conn->badged_server_ep_cspath.capPtr = ads_ep;
  server->pd_conn = malloc(sizeof(pd_client_context_t));
  server->pd_conn->badged_server_ep_cspath.capPtr = pd_ep;
  server->next_slot = vka_next_slot_fn;
  server->badge_ep = vka_badge_ep_fn;

  /* Get a CPtr to the parent's root cnode. */
  vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

  /* Allocate the Endpoint that the server will be listening on. */
  vka_object_t server_ep_obj;
  error = vka_alloc_endpoint(parent_vka, &server_ep_obj);
  server->server_ep = server_ep_obj.cptr;

  /* Configure thread */
  sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                           parent_cspace_cspath.root,
                                                           seL4_NilData,
                                                           server->server_ep,
                                                           priority);

  sel4utils_thread_t server_thread;
  error = sel4utils_configure_thread_config(parent_vka,
                                            parent_vspace,
                                            parent_vspace,
                                            config,
                                            &server_thread);
  CHECK_ERROR_GOTO(error, "sel4utils_configure_thread failed", out);

  NAME_THREAD(server_thread.tcb.cptr, "xv6fs server");
  error = sel4utils_start_thread(&server_thread,
                                 (sel4utils_thread_entry_fn)&xv6fs_server_main,
                                 NULL, NULL, 1);
  CHECK_ERROR_GOTO(error, "sel4utils_start_thread failed", out);

  return 0;

out:
  XV6FS_PRINTF("spawn_thread: Server ran into an error.\n");
  vka_free_object(parent_vka, &server_ep_obj); // ARYA-TODO does this unmap?
  return error;
}

uint64_t fs_assign_new_badge(uint64_t fd)
{
  // Add the blockno to the badge
  seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_FILE,
                                      0x00,
                                      0x00,
                                      fd);

  assert(badge_val != 0);
  XV6FS_PRINTF("fs_assign_new_badge: new badge: %lx\n", badge_val);
  return badge_val;
}

/**
 * To be run once at the beginning of fs main
 */
static int fs_init()
{
  xv6fs_server_context_t *server = get_xv6fs_server();
  int error;

  /* Initialize the blocks */
  error = init_naive_blocks();
  CHECK_ERROR(error, "failed to initialize the blocks");

  /* Allocate the TEMP shared memory object */
  server->shared_mem = malloc(sizeof(mo_client_context_t));
  seL4_CPtr free_slot;
  error = get_xv6fs_server()->next_slot(&free_slot);
  CHECK_ERROR(error, "failed to get next cspace slot");

  error = mo_component_client_connect(server->gpi_ep,
                                      free_slot,
                                      1,
                                      server->shared_mem);
  CHECK_ERROR(error, "failed to allocate shared mem page");

  error = ads_client_attach(server->ads_conn,
                            NULL,
                            server->shared_mem,
                            &server->shared_mem_vaddr);
  CHECK_ERROR(error, "failed to map shared mem page");

  /* Initialize the fs */
  error = init_disk_file();
  CHECK_ERROR(error, "failed to initialize disk file");
  binit();
  fileinit();
  fsinit(ROOTDEV);

  XV6FS_PRINTF("Initialized file system\n");

  /* Send our ep to the parent process */
  XV6FS_PRINTF("Messaging parent process at slot %d, sending ep %d\n", (int)server->parent_ep, (int)server->server_ep);

  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 0);
  seL4_SetCap(0, server->server_ep);
  seL4_Send(server->parent_ep, tag);

  return error;
}

/**
 * @brief The starting point for the xv6fs server's thread.
 *
 */
int xv6fs_server_main()
{
  XV6FS_PRINTF("started\n");

  seL4_MessageInfo_t tag;
  seL4_Error error = 0;
  seL4_Word sender_badge;
  cspacepath_t received_cap_path;
  received_cap_path.root = PD_CAP_ROOT;
  received_cap_path.capDepth = PD_CAP_DEPTH;

  error = fs_init();
  CHECK_ERROR_GOTO(error, "failed to initialize fs", exit_main);

  while (1)
  {
    /* Alloc cap receive slot*/
    error = get_xv6fs_server()->next_slot(&received_cap_path.capPtr);
    CHECK_ERROR_GOTO(error, "failed to alloc cap receive slot", exit_main);

    seL4_SetCapReceivePath(
        received_cap_path.root,
        received_cap_path.capPtr,
        received_cap_path.capDepth);

    /* Receive a message */
    tag = recv(&sender_badge);
    unsigned int op = seL4_GetMR(FSMSGREG_FUNC);

    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

    if (sender_badge == 0)
    { /* Handle Untyped Request */
      switch (op)
      {
      case FS_FUNC_CREATE_REQ:
        // Assign a new FD to this ep
        // (XXX) Arya: use proper shared memory for name eventually
        // const char *pathname = get_xv6fs_server()->shared_mem;
        seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_CREATE_ACK_END);

        // Open (or create) the file
        char *pathname = "test-file";
        int open_mode = O_CREAT | O_RDWR;
        struct file *file = xv6fs_sys_open(pathname, open_mode);
        error = file == NULL ? FS_SERVER_ERROR_UNKNOWN : FS_SERVER_NOERROR;
        CHECK_ERROR_GOTO(error, "Failed to open file", done);
        XV6FS_PRINTF("Server opened file %s\n", pathname);

        // Add to registry if not already present
        fs_registry_entry_t *reg_entry = fs_registry_get_entry_by_id(file->id);

        if (reg_entry == NULL)
        {
          reg_entry = malloc(sizeof(fs_registry_entry_t));
          reg_entry->count = 0;
          reg_entry->file = file;
          fs_registry_insert(reg_entry);
        }

        // Create the badged endpoint
        seL4_Word badge = fs_assign_new_badge(file->id);
        seL4_CPtr badged_ep;

        error = get_xv6fs_server()->badge_ep(badge, &badged_ep);
        CHECK_ERROR_GOTO(error, "failed to mint client badge", done);

        /* Return this badged end point in the return message. */
        seL4_SetCap(0, badged_ep);
        reply_tag = seL4_MessageInfo_set_extraCaps(reply_tag, 1);

        XV6FS_PRINTF("Replying with badged EP: ");
        badge_print(badge);
        printf("\n");
        break;
      default:
        CHECK_ERROR_GOTO(1, "got invalid op on unbadged ep", done);
      }
    }
    else
    {
      /* Handle Typed Request */
      int ret;
      fs_registry_entry_t *reg_entry = fs_registry_get_entry_by_badge(sender_badge);
      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid badge");
        error = FS_SERVER_ERROR_BADGE;
        goto done;
      }

      XV6FS_PRINTF("Got request for file with id %ld\n", reg_entry->file->id);

      switch (op)
      {
      case FS_FUNC_READ_REQ:
      case FS_FUNC_WRITE_REQ:
        int n_bytes_to_read = seL4_GetMR(FSMSGREG_READ_REQ_N);
        int offset = seL4_GetMR(FSMSGREG_READ_REQ_OFFSET);

        /* Attach memory object to server ADS */
        CHECK_ERROR_GOTO(seL4_MessageInfo_get_extraCaps(tag) != 1,
                         "client did not attach MO for read/write op", done);
        mo_client_context_t mo_conn;
        mo_conn.badged_server_ep_cspath = received_cap_path;
        void *mo_vaddr;
        error = ads_client_attach(get_xv6fs_server()->ads_conn,
                                  NULL,
                                  &mo_conn,
                                  &mo_vaddr);
        CHECK_ERROR_GOTO(error, "failed to attach client's MO to ADS", done);

        // Perform file read / write
        int n_bytes_ret;
        if (op == FS_FUNC_READ_REQ)
        {
          n_bytes_ret = xv6fs_sys_read(reg_entry->file, mo_vaddr, n_bytes_to_read, offset);
          XV6FS_PRINTF("Read %d bytes from file\n", n_bytes_ret);
        }
        else
        {
          n_bytes_ret = xv6fs_sys_write(reg_entry->file, mo_vaddr, n_bytes_to_read, offset);
          XV6FS_PRINTF("Wrote %d bytes to file\n", n_bytes_ret);
        }

        seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_READ_ACK_END);
        seL4_SetMR(FSMSGREG_READ_ACK_N, n_bytes_ret);
        break;
#if 0 // (XXX) Arya: to remove
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
#endif
      default:
        ZF_LOGE(XV6FS_S "%s: got unexpected opcode %d\n",
                __func__,
                op);

        error = FS_SERVER_ERROR_UNKNOWN;
      }
    }
  done:
    seL4_MessageInfo_ptr_set_label(&reply_tag, error);
    reply(reply_tag);
  }

exit_main:
  XV6FS_PRINTF("main: Suspending.");
  return -1;
}

static int block_read(uint blockno, void *buf)
{
  XV6FS_PRINTF("Reading blockno %d\n", blockno);
  int error = ramdisk_client_read(&get_xv6fs_server()->naive_blocks[blockno],
                                  get_xv6fs_server()->shared_mem);

  if (error == 0)
  {
    memcpy(buf, get_xv6fs_server()->shared_mem_vaddr, RAMDISK_BLOCK_SIZE);
  }

  return error;
}

static int block_write(uint blockno, void *buf)
{
  XV6FS_PRINTF("Writing blockno %d\n", blockno);
  memcpy(get_xv6fs_server()->shared_mem_vaddr, buf, RAMDISK_BLOCK_SIZE);
  return ramdisk_client_write(&get_xv6fs_server()->naive_blocks[blockno],
                              get_xv6fs_server()->shared_mem);
}

/* Override xv6 block read/write functions */
void xv6fs_bread(uint blockno, void *buf)
{
  block_read(blockno, buf);
}

void xv6fs_bwrite(uint blockno, void *buf)
{
  block_write(blockno, buf);
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
