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
#include <sel4gpi/resource_server_utils.h>
#include <ramdisk_client.h>

#include <libc_fs_helpers.h>
#include <fs_shared.h>
#include <fs_server.h>
#include <defs.h>
#include <file.h>
#include <buf.h>

#if FS_DEBUG
#define XV6FS_PRINTF(...)   \
  do                        \
  {                         \
    printf("%s ", XV6FS_S); \
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
      ZF_LOGE(XV6FS_S "%s: %s"  \
                      ", %d.",  \
              __func__,         \
              msg,              \
              error);           \
      return error;             \
    }                           \
  } while (0);

#define CHECK_ERROR_GOTO(check, msg, err, dest) \
  do                                            \
  {                                             \
    if ((check) != seL4_NoError)                \
    {                                           \
      ZF_LOGE(XV6FS_S "%s: %s"                  \
                      ", %d.",                  \
              __func__,                         \
              msg,                              \
              error);                           \
      error = err;                              \
      goto dest;                                \
    }                                           \
  } while (0);

/* Used by xv6fs internal functions when they panic */
__attribute__((noreturn)) void xv6fs_panic(char *s)
{
  printf("panic: %s\n", s);
  for (;;)
    ;
}

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

/**
 * @brief Lookup the client registry entry for the given badge.
 *
 * @param badge
 * @return fs_registry_entry_t*
 */
static void fs_registry_remove(fs_registry_entry_t *entry)
{
  fs_registry_entry_t *current_ctx = get_xv6fs_server()->client_registry;

  // Check if entry to remove is head of list
  if (current_ctx == entry)
  {
    get_xv6fs_server()->client_registry = entry->next;
    free(entry->file);
    free(entry);
    return;
  }

  // Otherwise remove from list
  while (current_ctx != NULL)
  {
    if (current_ctx->next == entry)
    {
      current_ctx->next = entry->next;
      free(entry->file);
      free(entry);
      return;
    }
    current_ctx = current_ctx->next;
  }
}

/*--- XV6FS SERVER ---*/
static xv6fs_server_context_t xv6fs_server;

xv6fs_server_context_t *get_xv6fs_server(void)
{
  return &xv6fs_server;
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
    error = resource_server_next_slot(&get_xv6fs_server()->gen, &free_slot);

    CHECK_ERROR(error, "failed to get a free slot");
    error = ramdisk_client_alloc_block(ramdisk_ep, NULL, free_slot, &get_xv6fs_server()->naive_blocks[i]);
    CHECK_ERROR(error, "failed to alloc a block from ramdisk");
  }

  return 0;
}

int xv6fs_server_spawn_thread(simple_t *parent_simple,
                              vka_t *parent_vka,
                              vspace_t *parent_vspace,
                              seL4_CPtr gpi_ep,
                              seL4_CPtr rd_ep,
                              seL4_CPtr parent_ep,
                              seL4_CPtr ads_ep,
                              seL4_CPtr pd_ep,
                              uint8_t priority)
{
  get_xv6fs_server()->rd_ep = rd_ep;

  return resource_server_spawn_thread(
      &get_xv6fs_server()->gen,
      parent_simple,
      parent_vka,
      parent_vspace,
      gpi_ep,
      parent_ep,
      ads_ep,
      priority,
      "fs server",
      xv6fs_server_main);
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
  error = resource_server_next_slot(&get_xv6fs_server()->gen, &free_slot);

  CHECK_ERROR(error, "failed to get next cspace slot");

  error = mo_component_client_connect(server->gen.gpi_ep,
                                      free_slot,
                                      1,
                                      server->shared_mem);
  CHECK_ERROR(error, "failed to allocate shared mem page");

  error = ads_client_attach(server->gen.ads_conn,
                            NULL,
                            server->shared_mem,
                            &server->shared_mem_vaddr);
  CHECK_ERROR(error, "failed to map shared mem page");

  /* Initialize the fs */
  error = init_disk_file();
  CHECK_ERROR(error, "failed to initialize disk file");
  binit();
  fsinit(ROOTDEV);

  XV6FS_PRINTF("Initialized file system\n");

  /* Send our ep to the parent process */
  XV6FS_PRINTF("Messaging parent process at slot %d, sending ep %d\n", (int)server->gen.parent_ep, (int)server->gen.server_ep);

  seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 0);
  seL4_SetCap(0, server->gen.server_ep);
  seL4_Send(server->gen.parent_ep, tag);

  return error;
}

static seL4_MessageInfo_t xv6fs_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap)
{
  int error;
  void *mo_vaddr;

  unsigned int op = seL4_GetMR(FSMSGREG_FUNC);
  uint64_t obj_id = get_object_id_from_badge(sender_badge);

  seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

  if (obj_id == 0)
  { /* Handle Untyped Request */
    switch (op)
    {
    case FS_FUNC_CREATE_REQ:
      int open_flags = seL4_GetMR(FSMSGREG_CREATE_REQ_FLAGS);

      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      // Open (or create) the file
      char *pathname = (char *)mo_vaddr;
      XV6FS_PRINTF("Server opening file %s, flags 0x%x\n", pathname, open_flags);
      struct file *file = xv6fs_sys_open(pathname, open_flags);
      error = file == NULL ? FS_SERVER_ERROR_UNKNOWN : FS_SERVER_NOERROR;
      if (error != 0)
      {
        // Don't announce failed to open files as error, not usually an error
        XV6FS_PRINTF("File did not exist\n");
        error = FS_SERVER_ERROR_NOFILE;
        goto done;
      }

      // Add to registry if not already present
      fs_registry_entry_t *reg_entry = fs_registry_get_entry_by_id(file->id);

      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("File not previously open, make new registry entry\n");
        reg_entry = malloc(sizeof(fs_registry_entry_t));
        reg_entry->count = 1;
        reg_entry->file = file;
        fs_registry_insert(reg_entry);
      }
      else
      {
        XV6FS_PRINTF("File was already open, use previous registry entry\n");
        reg_entry->count++;
        free(file); // We don't need another copy of the structure
        file = reg_entry->file;
      }

      // Create the badged endpoint
      seL4_Word badge = fs_assign_new_badge(file->id);
      seL4_CPtr badged_ep;

      error = resource_server_badge_ep(&get_xv6fs_server()->gen,
                                       badge, &badged_ep);
      CHECK_ERROR_GOTO(error, "failed to mint client badge", error, done);

      /* Return this badged end point in the return message. */
      seL4_SetCap(0, badged_ep);
      seL4_MessageInfo_ptr_set_extraCaps(&reply_tag, 1);
      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_CREATE_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_CREATE_ACK);

      XV6FS_PRINTF("Replying with badged EP: ");
      badge_print(badge);
      XV6FS_PRINTF("\n");
      break;
    case FS_FUNC_UNLINK_REQ:
      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      pathname = (char *)mo_vaddr;
      XV6FS_PRINTF("Unlink pathname %s\n", pathname);
      error = xv6fs_sys_unlink(pathname);

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_UNLINK_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_UNLINK_ACK);
      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid op on unbadged ep", error, done);
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
    case RS_FUNC_GET_RR_REQ:
      XV6FS_PRINTF("Get RR for fileno %ld\n", reg_entry->file->id);

      size_t mo_size = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE);

      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      // Initialize the model state
      model_state_t *model_state = (model_state_t *)mo_vaddr;
      init_model_state(model_state);
      csv_row_t *row_ptr = mo_vaddr + sizeof(model_state_t);

      // Add the entry for the resource
      // (XXX) Arya: fileno may not be globally unique, need combined ID
      char file_res_id[CSV_MAX_STRING_SIZE];
      snprintf(file_res_id, CSV_MAX_STRING_SIZE, "%s_%lu", FILE_RESOURCE_NAME, reg_entry->file->id);
      add_resource_row(model_state, FILE_RESOURCE_NAME, file_res_id, row_ptr);
      row_ptr++;

      // Add relations for blocks
      int n_blocknos = 100; // (XXX) Arya: what if there are more blocks?
      int *blocknos = malloc(sizeof(int) * n_blocknos);
      xv6fs_sys_blocknos(reg_entry->file, blocknos, n_blocknos, &n_blocknos);
      XV6FS_PRINTF("File has %d blocks\n", n_blocknos);

      char block_res_id[CSV_MAX_STRING_SIZE];

      for (int i = 0; i < n_blocknos; i++)
      {
        if ((void *)(row_ptr + 1) >= mo_vaddr + mo_size)
        {
          XV6FS_PRINTF("Ran out of space in the MO to write RR, wrote %d rows\n", i);
          error = RS_ERROR_RR_SIZE;
          break;
        }
        snprintf(block_res_id, CSV_MAX_STRING_SIZE, "BLOCK_%u", blocknos[i]);
        add_resource_depends_on_row(model_state, file_res_id, block_res_id, row_ptr);
        row_ptr++;
      }
      free(blocknos);

      seL4_SetMR(RDMSGREG_FUNC, RS_FUNC_GET_RR_ACK);
      break;
    case FS_FUNC_READ_REQ:
      int n_bytes_to_read = seL4_GetMR(FSMSGREG_READ_REQ_N);
      int offset = seL4_GetMR(FSMSGREG_READ_REQ_OFFSET);

      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      // Perform file read
      int n_bytes_ret = xv6fs_sys_read(reg_entry->file, mo_vaddr, n_bytes_to_read, offset);
      XV6FS_PRINTF("Read %d bytes from file\n", n_bytes_ret);

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_READ_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_READ_ACK);
      seL4_SetMR(FSMSGREG_READ_ACK_N, n_bytes_ret);
      break;
    case FS_FUNC_WRITE_REQ:
      n_bytes_to_read = seL4_GetMR(FSMSGREG_READ_REQ_N);
      offset = seL4_GetMR(FSMSGREG_READ_REQ_OFFSET);

      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      // Perform file write
      n_bytes_ret = xv6fs_sys_write(reg_entry->file, mo_vaddr, n_bytes_to_read, offset);
      XV6FS_PRINTF("Wrote %d bytes to file\n", n_bytes_ret);

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_WRITE_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_WRITE_ACK);
      seL4_SetMR(FSMSGREG_WRITE_ACK_N, n_bytes_ret);
      break;
    case FS_FUNC_CLOSE_REQ:
        /* Cleanup resources associated with the file */;
      reg_entry->count--;
      if (reg_entry->count <= 0)
      {
        XV6FS_PRINTF("Removing registry entry for file with 0 refcount\n");
        fs_registry_remove(reg_entry);
      }

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_CLOSE_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_CLOSE_ACK);
      break;
    case FS_FUNC_STAT_REQ:
      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      /* Call function stat */
      error = xv6fs_sys_stat(reg_entry->file, (struct stat *)mo_vaddr);

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_STAT_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_STAT_ACK);
      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid op on badged ep", FS_SERVER_ERROR_UNKNOWN, done);
    }
  }

done:
  seL4_MessageInfo_ptr_set_label(&reply_tag, error);
  return reply_tag;
}

/**
 * @brief The starting point for the xv6fs server's thread.
 *
 */
int xv6fs_server_main()
{
  XV6FS_PRINTF("Started\n");

  int error = fs_init();
  CHECK_ERROR(error, "failed to initialize fs");

  return resource_server_main(&get_xv6fs_server()->gen,
                              xv6fs_request_handler);
}

static int block_read(uint32_t blockno, void *buf)
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

static int block_write(uint32_t blockno, void *buf)
{
  XV6FS_PRINTF("Writing blockno %d\n", blockno);
  memcpy(get_xv6fs_server()->shared_mem_vaddr, buf, RAMDISK_BLOCK_SIZE);
  return ramdisk_client_write(&get_xv6fs_server()->naive_blocks[blockno],
                              get_xv6fs_server()->shared_mem);
}

/* Override xv6 block read/write functions */
void xv6fs_bread(uint32_t blockno, void *buf)
{
  block_read(blockno, buf);
}

void xv6fs_bwrite(uint32_t blockno, void *buf)
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
