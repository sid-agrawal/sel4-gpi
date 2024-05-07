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
 * Make the path prefix for a namespace
 */
static void make_ns_prefix(char *prefix, uint64_t nsid)
{
  snprintf(prefix, PATH_MAX, "/ns%ld", nsid);
}

/**
 * Add the prefix to a path, overwriting path
 */
static void apply_prefix(char *prefix, char *path)
{
  char temp[PATH_MAX];

  if (strlen(path) > 0 && path[0] == '/')
  {
    // Don't need to add path separator
    snprintf(temp, PATH_MAX, "%s%s", prefix, path);
  }
  else
  {
    snprintf(temp, PATH_MAX, "%s/%s", prefix, path);
  }

  strcpy(path, temp);
}

/**
 * Insert the data for a namespace
 */
static void insert_ns(fs_namespace_t *ns)
{
  ns->next = get_xv6fs_server()->namespaces;
  get_xv6fs_server()->namespaces = ns;
}

/**
 * Find the data for a namespace
 */
static fs_namespace_t *find_ns(uint64_t nsid)
{
  for (fs_namespace_t *curr = get_xv6fs_server()->namespaces; curr != NULL; curr = curr->next)
  {
    if (curr->id == nsid)
    {
      return curr;
    }
  }

  return NULL;
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
 * @param object_id
 * @return fs_registry_entry_t*
 */
static fs_registry_entry_t *fs_registry_get_entry_by_id(uint64_t object_id)
{
  fs_registry_entry_t *current_ctx = get_xv6fs_server()->client_registry;
  uint64_t file_id = get_local_object_id_from_badge(object_id);

  while (current_ctx != NULL)
  {
    if ((seL4_Word)current_ctx->file->id == file_id)
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

  uint64_t object_id = get_object_id_from_badge(badge);
  return fs_registry_get_entry_by_id(object_id);
}

/**
 * @brief Close the registry entry
 * If the entry has no references, close the corresponding file
 *
 * @param entry entry to close
 * @return fs_registry_entry_t*
 */
static void fs_registry_close(fs_registry_entry_t *entry)
{
  fs_registry_entry_t *current_ctx = get_xv6fs_server()->client_registry;

  // Check if entry to remove is head of list
  if (current_ctx == entry)
  {
    current_ctx->count--;

    if (current_ctx->count == 0)
    {
      get_xv6fs_server()->client_registry = entry->next;
      xv6fs_sys_fileclose(entry->file);
      free(entry);
    }

    return;
  }

  // Otherwise remove from list
  while (current_ctx != NULL)
  {
    if (current_ctx->next == entry)
    {
      current_ctx->next->count--;

      if (current_ctx->next->count == 0)
      {
        current_ctx->next = entry->next;
        xv6fs_sys_fileclose(entry->file);
        free(entry);
      }

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
    error = ramdisk_client_alloc_block(ramdisk_ep,
                                       &get_xv6fs_server()->naive_blocks[i]);
    CHECK_ERROR(error, "failed to alloc a block from ramdisk");
  }

  return 0;
}

/**
 * To be run once at the beginning of fs main
 */
int xv6fs_init()
{
  xv6fs_server_context_t *server = get_xv6fs_server();
  int error;

  /* Allocate the shared memory object used to communicate with the ramdisk*/
  server->shared_mem = malloc(sizeof(mo_client_context_t));
  seL4_CPtr free_slot;
  error = resource_server_next_slot(&get_xv6fs_server()->gen, &free_slot);

  CHECK_ERROR(error, "failed to get next cspace slot");

  error = mo_component_client_connect(server->gen.mo_ep,
                                      free_slot,
                                      1,
                                      server->shared_mem);
  CHECK_ERROR(error, "failed to allocate shared mem page");

  error = ads_client_attach(&server->gen.ads_conn,
                            NULL,
                            server->shared_mem,
                            SEL4UTILS_RES_TYPE_SHARED_FRAMES,
                            &server->shared_mem_vaddr);
  CHECK_ERROR(error, "failed to map shared mem page");

  error = ramdisk_client_bind(get_xv6fs_server()->rd_ep, server->shared_mem);
  CHECK_ERROR(error, "failed to bind shared mem page");

  /* Initialize the blocks */
  error = init_naive_blocks();
  CHECK_ERROR(error, "failed to initialize the blocks");

  /* Initialize the fs */
  error = init_disk_file();
  CHECK_ERROR(error, "failed to initialize disk file");
  binit();
  fsinit(ROOTDEV);

  XV6FS_PRINTF("Initialized file system\n");

  return error;
}

seL4_MessageInfo_t xv6fs_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap, bool *need_new_recv_cap)
{
  int error;
  void *mo_vaddr;
  *need_new_recv_cap = true; // (XXX) Arya: todo, find the cases when we actually need this
  unsigned int op = seL4_GetMR(FSMSGREG_FUNC);
  uint64_t obj_id = get_object_id_from_badge(sender_badge);

  seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

  if (sender_badge == 0)
  { /* Handle Unbadged Request */
    XV6FS_PRINTF("Received unbadged request\n");

    switch (op)
    {
    case RS_FUNC_GET_RR_REQ:
      *need_new_recv_cap = false;

      uint64_t pd_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_PD_ID);
      uint64_t fs_pd_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_RS_PD_ID);

      void *mem_vaddr = (void *)seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR);
      model_state_t *model_state = (model_state_t *)mem_vaddr;
      void *free_mem = mem_vaddr + sizeof(model_state_t);
      size_t free_size = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE) - sizeof(model_state_t);

// (XXX) Arya: Switch from dumping one resource to dumping entire namespace
#if 1
      uint64_t ns_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_ID);
      XV6FS_PRINTF("Get RR for ns %d\n", ns_id);

      /* Update pathname for namespace */
      char path[MAXPATH];
      strcpy(path, ROOT_DIR);

      if (ns_id != NSID_DEFAULT)
      {
        fs_namespace_t *ns = find_ns(ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist for dumprr\n");
          error = RS_ERROR_NS;
          goto done;
        }

        apply_prefix(ns->ns_prefix, path);
      }

      /* List all the files in the NS */
      int n_files;
      uint32_t inums[16];

      error = xv6fs_sys_walk(path, false, inums, &n_files);
      CHECK_ERROR_GOTO(error, "Failed to walk FS", FS_SERVER_ERROR_UNKNOWN, done);

      /* Initialize the model state */
      init_model_state(model_state, free_mem, free_size);

      // (XXX) Arya: A lot of this should be moved to PD component once we have resource spaces implemented

      /* Add the PD nodes */
      gpi_model_node_t *self_pd_node = add_pd_node(model_state, NULL, fs_pd_id);
      gpi_model_node_t *client_pd_node = add_pd_node(model_state, NULL, pd_id);

      /* Add the file resource space node */
      gpi_model_node_t *file_space_node = add_resource_space_node(model_state, GPICAP_TYPE_FILE, get_xv6fs_server()->gen.server_id);
      add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, file_space_node);

      /* Add the block resource space node */
      // (XXX) Arya: Assumes there is only one block space, and it is space 1. To fix.
      int block_space_id = 1;
      gpi_model_node_t *block_space_node = add_resource_space_node(model_state, GPICAP_TYPE_BLOCK, block_space_id);
      add_edge(model_state, GPI_EDGE_TYPE_MAP, file_space_node, block_space_node);

      /* Add nodes for all files and blocks */
      int n_blocknos = 100; // (XXX) Arya: what if there are more blocks?
      int *blocknos = malloc(sizeof(int) * n_blocknos);
      for (int i = 0; i < n_files; i++)
      {
        XV6FS_PRINTF("Get RR for fileno %ld\n", inums[i]);

        /* Add the file resource node */
        gpi_model_node_t *file_node = add_resource_node(model_state, GPICAP_TYPE_FILE, get_xv6fs_server()->gen.server_id, inums[i]);
        add_edge(model_state, GPI_EDGE_TYPE_HOLD, self_pd_node, file_node);
        add_edge(model_state, GPI_EDGE_TYPE_HOLD, client_pd_node, file_node);
        add_edge(model_state, GPI_EDGE_TYPE_SUBSET, file_node, file_space_node);

        /* Add relations for blocks */
        error = xv6fs_sys_inode_blocknos(inums[i], blocknos, n_blocknos, &n_blocknos);
        CHECK_ERROR_GOTO(error, "Failed to get blocknos for file", FS_SERVER_ERROR_UNKNOWN, done);
        XV6FS_PRINTF("File has %d blocks\n", n_blocknos);

        for (int j = 0; j < n_blocknos; j++)
        {
          uint64_t block_id = get_xv6fs_server()->naive_blocks[blocknos[j]].id;

          gpi_model_node_t *block_node = add_resource_node(model_state, GPICAP_TYPE_BLOCK, block_space_id, block_id);
          add_edge(model_state, GPI_EDGE_TYPE_MAP, file_node, block_node);
        }
      }

      free(blocknos);

#else
      uint64_t resource_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_ID);

      // Find the resource
      fs_registry_entry_t *reg_entry = fs_registry_get_entry_by_id(resource_id);
      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid resource for RR request, ID is 0x%lx, local ID is 0x%lx\n",
                     resource_id, get_local_object_id_from_badge(resource_id));
        error = RS_ERROR_DNE;

        // (XXX) Arya: Ideally, we should have let the PD component know tha this file was deleted
        // For now, just return RS_ERROR_DNE
        goto done;
      }

      XV6FS_PRINTF("Get RR for fileno %ld\n", reg_entry->file->id);

      // Add the entry for the resource
      // (XXX) Arya: fileno may not be globally unique, need combined ID
      char file_res_id[CSV_MAX_STRING_SIZE];
      make_res_id(file_res_id, GPICAP_TYPE_FILE, resource_id);
      add_resource_rr(rr_state, GPICAP_TYPE_FILE, file_res_id, row_ptr);
      row_ptr++;

      // Add relations for blocks
      int n_blocknos = 100; // (XXX) Arya: what if there are more blocks?
      int *blocknos = malloc(sizeof(int) * n_blocknos);
      xv6fs_sys_blocknos(reg_entry->file, blocknos, n_blocknos, &n_blocknos);
      XV6FS_PRINTF("File has %d blocks\n", n_blocknos);

      char block_res_id[CSV_MAX_STRING_SIZE];

      for (int i = 0; i < n_blocknos; i++)
      {
        if ((void *)(row_ptr + 1) >= mem_vaddr + mem_size)
        {
          XV6FS_PRINTF("Ran out of space in the MO to write RR, wrote %d rows\n", i);
          error = RS_ERROR_RR_SIZE;
          break;
        }

        uint64_t block_id = get_xv6fs_server()->naive_blocks[blocknos[i]].id;
        make_res_id(block_res_id, GPICAP_TYPE_BLOCK, block_id);
        add_resource_depends_on_rr(rr_state, file_res_id, block_res_id, REL_TYPE_MAP, row_ptr);
        row_ptr++;
      }
      free(blocknos);
#endif

      clean_model_state(model_state);

      seL4_SetMR(RDMSGREG_FUNC, RS_FUNC_GET_RR_ACK);
      break;
    default:
      XV6FS_PRINTF("Op is %d\n", op);
      CHECK_ERROR_GOTO(1, "got invalid op on unbadged ep", error, done);
    }
  }
  else if (obj_id == BADGE_OBJ_ID_NULL)
  { /* Handle Request Not Associated to Object */
    XV6FS_PRINTF("Received badged request with no object id\n");

    char *pathname;

    switch (op)
    {
    case RS_FUNC_NEW_NS_REQ:
      XV6FS_PRINTF("Got request for new namespace\n");

      uint64_t ns_id;

      // Register NS and get new ID
      error = resource_server_new_ns(&get_xv6fs_server()->gen, get_client_id_from_badge(sender_badge), &ns_id);
      XV6FS_PRINTF("Registered new namespace with ID %ld\n", ns_id);

      // Bookkeeping the NS
      fs_namespace_t *ns_entry = malloc(sizeof(fs_namespace_t));
      ns_entry->id = ns_id;
      make_ns_prefix(ns_entry->ns_prefix, ns_id);
      insert_ns(ns_entry);

      // Create directory in global FS
      error = xv6fs_sys_mkdir(ns_entry->ns_prefix);
      CHECK_ERROR_GOTO(error, "Failed to make new directory for namespace\n", FS_SERVER_ERROR_UNKNOWN, done);

      seL4_MessageInfo_ptr_set_length(&reply_tag, RSMSGREG_NEW_NS_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, RS_FUNC_NEW_NS_ACK);
      seL4_SetMR(RSMSGREG_NEW_NS_ACK_ID, ns_id);
      break;
    case FS_FUNC_CREATE_REQ:
      int open_flags = seL4_GetMR(FSMSGREG_CREATE_REQ_FLAGS);

      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);
      pathname = (char *)mo_vaddr;

      /* Update pathname if within a namespace */
      ns_id = get_ns_id_from_badge(sender_badge);
      if (ns_id != NSID_DEFAULT)
      {
        fs_namespace_t *ns = find_ns(ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist\n");
          error = RS_ERROR_NS;
          goto done;
        }

        apply_prefix(ns->ns_prefix, pathname);
      }

      // Open (or create) the file
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

        // Notify the PD component about the new reousrce
        error = resource_server_create_resource(&get_xv6fs_server()->gen, file->id);
        CHECK_ERROR_GOTO(error, "Failed to create the resource", error, done);
      }
      else
      {
        XV6FS_PRINTF("File was already open, use previous registry entry\n");
        reg_entry->count++;

        fileclose(file); // We don't need another copy of the structure
        file = reg_entry->file;
        filedup(file);
      }

#if FS_DEBUG
      // Prints the FS contents to console for debug
      int n_files;
      uint32_t inums[16];
      error = xv6fs_sys_walk(ROOT_DIR, true, inums, &n_files);
      CHECK_ERROR_GOTO(error, "Failed to walk FS", FS_SERVER_ERROR_UNKNOWN, done);
#endif

      // Create the resource endpoint
      seL4_CPtr dest;
      error = resource_server_give_resource(&get_xv6fs_server()->gen,
                                            get_ns_id_from_badge(sender_badge),
                                            file->id,
                                            get_client_id_from_badge(sender_badge),
                                            &dest);
      CHECK_ERROR_GOTO(error, "Failed to give the resource", error, done);

      // Unattach the MO
      error = resource_server_unattach(&get_xv6fs_server()->gen, mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to unattach MO", error, done);

      // Send the reply
      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_CREATE_ACK_END);
      seL4_SetMR(FSMSGREG_CREATE_ACK_DEST, dest);
      seL4_SetMR(RSMSGREG_FUNC, FS_FUNC_CREATE_ACK);
      break;
    case FS_FUNC_LINK_REQ:
      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);
      pathname = (char *)mo_vaddr;

      /* Update pathname if within a namespace */
      ns_id = get_ns_id_from_badge(sender_badge);
      if (ns_id != NSID_DEFAULT)
      {
        fs_namespace_t *ns = find_ns(ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist\n");
          error = RS_ERROR_NS;
          goto done;
        }

        apply_prefix(ns->ns_prefix, pathname);
      }

      /* Find the file to link */
      seL4_Word file_badge = seL4_GetBadge(1);

      reg_entry = fs_registry_get_entry_by_badge(file_badge);
      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid file to link\n");
        error = FS_SERVER_ERROR_BADGE;
        goto done;
      }

      XV6FS_PRINTF("File to link has id %ld, linking to path %s\n", reg_entry->file->id, pathname);

      /* Do the link */
      error = xv6fs_sys_dolink2(reg_entry->file, pathname);

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_LINK_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_LINK_ACK);
      break;
    case FS_FUNC_UNLINK_REQ:
      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);
      pathname = (char *)mo_vaddr;

      /* Update pathname if within a namespace */
      ns_id = get_ns_id_from_badge(sender_badge);
      if (ns_id != NSID_DEFAULT)
      {
        fs_namespace_t *ns = find_ns(ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist\n");
          error = RS_ERROR_NS;
          goto done;
        }

        apply_prefix(ns->ns_prefix, pathname);
      }

      XV6FS_PRINTF("Unlink pathname %s\n", pathname);
      error = xv6fs_sys_unlink(pathname);
      CHECK_ERROR_GOTO(error, "Failed to unlink", FS_SERVER_ERROR_UNKNOWN, done);

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_UNLINK_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_UNLINK_ACK);
      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid op on badged ep without obj id", error, done);
    }
  }
  else
  {
    /* Handle Request On Specific Resource */
    XV6FS_PRINTF("Received badged request with object id %lx\n", get_object_id_from_badge(sender_badge));

    int ret;
    fs_registry_entry_t *reg_entry = fs_registry_get_entry_by_badge(sender_badge);
    if (reg_entry == NULL)
    {
      XV6FS_PRINTF("Received invalid badge\n");
      error = FS_SERVER_ERROR_BADGE;
      goto done;
    }

    XV6FS_PRINTF("Got request for file with id %ld\n", reg_entry->file->id);

    switch (op)
    {
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
      XV6FS_PRINTF("Close file (%d)\n", reg_entry->file->id);

      /* Remove the ref in the registry entry */
      fs_registry_close(reg_entry);

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
      CHECK_ERROR_GOTO(1, "got invalid op on badged ep with obj id", FS_SERVER_ERROR_UNKNOWN, done);
    }
  }

done:
  seL4_MessageInfo_ptr_set_label(&reply_tag, error);
  return reply_tag;
}

static int block_read(uint32_t blockno, void *buf)
{
  XV6FS_PRINTF("Reading blockno %d\n", blockno);
  int error = ramdisk_client_read(&get_xv6fs_server()->naive_blocks[blockno]);

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
  return ramdisk_client_write(&get_xv6fs_server()->naive_blocks[blockno]);
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
