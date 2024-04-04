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

/* FILE REGISTRY DS */
// (XXX) Arya: Replace this with more generic DS?

/**
 * @brief Insert a new client into the client registry Linked List.
 *
 * @param new_node
 */
static void file_registry_insert(file_registry_entry_t *new_node)
{
  file_registry_entry_t *head = get_xv6fs_server()->file_registry;

  new_node->next = head;
  get_xv6fs_server()->file_registry = new_node;
  get_xv6fs_server()->n_files++;

  return;
}

/**
 * @brief Lookup the file registry entry for the given id.
 *
 * @param object_id
 * @return file_registry_entry_t*
 */
static file_registry_entry_t *file_registry_get_entry_by_id(uint64_t object_id)
{
  file_registry_entry_t *current_ctx = get_xv6fs_server()->file_registry;
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
 * @return file_registry_entry_t*
 */
static file_registry_entry_t *file_registry_get_entry_by_badge(seL4_Word badge)
{

  uint64_t object_id = get_object_id_from_badge(badge);
  return file_registry_get_entry_by_id(object_id);
}

/**
 * @brief Remove the file registry entry
 *
 * @param badge
 * @return file_registry_entry_t*
 */
static void file_registry_remove(file_registry_entry_t *entry)
{
  file_registry_entry_t *current_ctx = get_xv6fs_server()->file_registry;

  // Check if entry to remove is head of list
  if (current_ctx == entry)
  {
    get_xv6fs_server()->file_registry = entry->next;
    free(entry->file);
    free(entry);
    get_xv6fs_server()->n_files--;
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
      get_xv6fs_server()->n_files--;
      return;
    }
    current_ctx = current_ctx->next;
  }
}

/* FILEPATH REGISTRY DS */

// Returns the filepath in the global namespace
static char *filepath_get_global_path(filepath_registry_entry_t *path_entry)
{
  if (path_entry->nsid == NSID_DEFAULT)
  {
    return path_entry->path;
  }
  else
  {
    return path_entry->global_path->path;
  }
}

// Returns the file linked to the filepath
static file_registry_entry_t *filepath_get_file(filepath_registry_entry_t *path_entry)
{
  if (path_entry->nsid == NSID_DEFAULT)
  {
    return path_entry->file;
  }
  else
  {
    return path_entry->global_path->file;
  }
}

/**
 * @brief Insert a new client into the filepath registry
 *
 * @param new_node
 */
static void filepath_registry_insert(filepath_registry_entry_t *new_node)
{
  filepath_registry_entry_t *head = get_xv6fs_server()->filepath_registry;

  new_node->next = head;
  get_xv6fs_server()->filepath_registry = new_node;
  get_xv6fs_server()->n_filepaths++;
}

/**
 * @brief Lookup the filepath registry entry for the given id.
 *
 * @param object_id
 * @return filepath_registry_entry_t*
 */
static filepath_registry_entry_t *filepath_registry_get_entry_by_id(uint64_t object_id)
{
  filepath_registry_entry_t *current_ctx = get_xv6fs_server()->filepath_registry;
  uint64_t filepath_id = get_local_object_id_from_badge(object_id);

  while (current_ctx != NULL)
  {
    if ((seL4_Word)current_ctx->id == filepath_id)
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
 * @return file_registry_entry_t*
 */
static filepath_registry_entry_t *filepath_registry_get_entry_by_badge(seL4_Word badge)
{
  uint64_t object_id = get_object_id_from_badge(badge);
  return filepath_registry_get_entry_by_id(object_id);
}

/**
 * @brief Lookup the filepath registry entry for the given namespace and path

 * @return filepath_registry_entry_t*
 */
static filepath_registry_entry_t *filepath_registry_get_entry_by_path(uint64_t nsid, char *path)
{
  filepath_registry_entry_t *current_ctx = get_xv6fs_server()->filepath_registry;

  while (current_ctx != NULL)
  {
    if ((seL4_Word)current_ctx->nsid == nsid && strcmp(current_ctx->path, path) == 0)
    {
      break;
    }
    current_ctx = current_ctx->next;
  }
  return current_ctx;
}

/**
 * @brief Remove the filepath registry entry
 *
 * @param badge
 * @return file_registry_entry_t*
 */
static void filepath_registry_remove(filepath_registry_entry_t *entry)
{
  filepath_registry_entry_t *current_ctx = get_xv6fs_server()->filepath_registry;

  // Check if entry to remove is head of list
  if (current_ctx == entry)
  {
    get_xv6fs_server()->filepath_registry = entry->next;
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

  /* Register self as file and filepath manager */
  error = resource_server_register_manager(&get_xv6fs_server()->gen, GPICAP_TYPE_FILE, &server->file_manager_id);
  CHECK_ERROR(error, "failed to register file manager");
  error = resource_server_register_manager(&get_xv6fs_server()->gen, GPICAP_TYPE_FILEPATH, &server->path_manager_id);
  CHECK_ERROR(error, "failed to register file manager");

  /* Initialize the blocks */
  error = init_naive_blocks();
  CHECK_ERROR(error, "failed to initialize the blocks");

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
                            &server->shared_mem_vaddr);
  CHECK_ERROR(error, "failed to map shared mem page");

  /* Initialize the fs */
  error = init_disk_file();
  CHECK_ERROR(error, "failed to initialize disk file");
  binit();
  fsinit(ROOTDEV);
  get_xv6fs_server()->n_files = 0;
  get_xv6fs_server()->n_filepaths = 0;

  /* Initialize the global namespace */
  fs_namespace_t *ns_entry = malloc(sizeof(fs_namespace_t));
  ns_entry->id = NSID_DEFAULT;
  strcpy(ns_entry->ns_prefix, "");
  insert_ns(ns_entry);

  /* Notify parent */
  uint64_t manager_ids[2] = {server->file_manager_id, server->path_manager_id};
  error = resource_server_notify_parent(&get_xv6fs_server()->gen, manager_ids, 2);
  CHECK_ERROR(error, "failed to notify parent that ramdisk started");

  XV6FS_PRINTF("Initialized file system\n");

  return error;
}

static seL4_MessageInfo_t xv6fs_untyped_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap)
{
  int error;
  unsigned int op = seL4_GetMR(FSMSGREG_FUNC);
  seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

  if (op == RS_FUNC_GET_RR_REQ)
  {
    uint64_t resource_id = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_ID);
    size_t mem_size = seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE);
    void *mem_vaddr = (void *)seL4_GetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR);
    uint64_t manager_id = get_server_id_from_badge(resource_id);

    if (manager_id == get_xv6fs_server()->path_manager_id)
    {
      // This is a request to extract RR for a filepath

      // Check memory size
      CHECK_ERROR_GOTO(mem_size < (sizeof(rr_state_t) + 2 * sizeof(csv_rr_row_t)),
                       "Shared memory for RR is too small", RS_ERROR_RR_SIZE, done1);

      // Initialize the model state
      rr_state_t *rr_state = (rr_state_t *)mem_vaddr;
      init_rr_state(rr_state);
      csv_rr_row_t *row_ptr = mem_vaddr + sizeof(rr_state_t);

      // Find the resource
      filepath_registry_entry_t *reg_entry = filepath_registry_get_entry_by_id(resource_id);
      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid resource for RR request, ID is 0x%lx, local ID is 0x%lx\n",
                     resource_id, get_local_object_id_from_badge(resource_id));
        error = RS_ERROR_DNE;

        // (XXX) Arya: Ideally, we should have let the PD component know tha this file was deleted
        // For now, just return RS_ERROR_DNE
        goto done1;
      }

      // Add the entry for the resource
      char path_res_id[CSV_MAX_STRING_SIZE];
      make_res_id(path_res_id, GPICAP_TYPE_FILEPATH, resource_id);
      add_resource_rr(rr_state, GPICAP_TYPE_FILEPATH, path_res_id, row_ptr);
      row_ptr++;

      if (reg_entry->nsid != NSID_DEFAULT)
      {
        // Add relation to global filepath
        char global_path_res_id[CSV_MAX_STRING_SIZE];
        make_res_id(global_path_res_id, GPICAP_TYPE_FILEPATH, reg_entry->global_path->id);
        add_resource_depends_on_rr(rr_state, path_res_id, global_path_res_id, row_ptr);
        row_ptr++;
      }
      else
      {
        // Add relation to the underlying file
        char file_res_id[CSV_MAX_STRING_SIZE];
        make_res_id(file_res_id, GPICAP_TYPE_FILEPATH, reg_entry->file->file->id);
        add_resource_depends_on_rr(rr_state, path_res_id, file_res_id, row_ptr);
        row_ptr++;
      }
    }
    else if (manager_id == get_xv6fs_server()->file_manager_id)
    {
      // This is a request to extract RR for a file
      // Initialize the model state
      rr_state_t *rr_state = (rr_state_t *)mem_vaddr;
      init_rr_state(rr_state);
      csv_rr_row_t *row_ptr = mem_vaddr + sizeof(rr_state_t);

      // Find the resource
      file_registry_entry_t *reg_entry = file_registry_get_entry_by_id(resource_id);
      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid resource for RR request, ID is 0x%lx, local ID is 0x%lx\n",
                     resource_id, get_local_object_id_from_badge(resource_id));
        error = RS_ERROR_DNE;

        // (XXX) Arya: Ideally, we should have let the PD component know tha this file was deleted
        // For now, just return RS_ERROR_DNE
        goto done1;
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
        add_resource_depends_on_rr(rr_state, file_res_id, block_res_id, row_ptr);
        row_ptr++;
      }
      free(blocknos);
    }
    else
    {
      CHECK_ERROR_GOTO(1, "got invalid manager ID", error, done1);
    }

    seL4_SetMR(RDMSGREG_FUNC, RS_FUNC_GET_RR_ACK);
  }
  else
  {
    XV6FS_PRINTF("Op is %d\n", op);
    CHECK_ERROR_GOTO(1, "got invalid op on unbadged ep", error, done1);
  }

done1:
  seL4_MessageInfo_ptr_set_label(&reply_tag, error);
  return reply_tag;
}

static seL4_MessageInfo_t xv6fs_filepath_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap)
{
  int error;
  void *mo_vaddr;
  uint64_t path_id;
  char *path;
  fs_namespace_t *ns;
  file_registry_entry_t *file_entry;
  filepath_registry_entry_t *reg_entry;
  seL4_CPtr dest;

  unsigned int op = seL4_GetMR(FSMSGREG_FUNC);
  uint64_t obj_id = get_object_id_from_badge(sender_badge);

  seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

  if (obj_id == BADGE_OBJ_ID_NULL)
  { /* Handle Request Not Associated to Object */
    XV6FS_PRINTF("Received badged request with no object id\n");

    char *pathname;

    switch (op)
    {
    case RS_FUNC_NEW_NS_REQ:
      XV6FS_PRINTF("Got request for new filepath namespace\n");

      uint64_t ns_id;

      // Register NS and get new ID
      error = resource_server_new_ns(&get_xv6fs_server()->gen,
                                     get_xv6fs_server()->path_manager_id,
                                     get_client_id_from_badge(sender_badge),
                                     &ns_id);
      XV6FS_PRINTF("Registered new namespace with ID %ld\n", ns_id);

      // Bookkeeping the NS
      ns = malloc(sizeof(fs_namespace_t));
      ns->id = ns_id;
      make_ns_prefix(ns->ns_prefix, ns_id);
      insert_ns(ns);

      // Create directory in global FS
      error = xv6fs_sys_mkdir(ns->ns_prefix);
      CHECK_ERROR_GOTO(error, "Failed to make new directory for namespace\n", FS_SERVER_ERROR_UNKNOWN, done2);

      seL4_MessageInfo_ptr_set_length(&reply_tag, RSMSGREG_NEW_NS_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, RS_FUNC_NEW_NS_ACK);
      seL4_SetMR(RSMSGREG_NEW_NS_ACK_ID, ns_id);
      break;
    case FS_FUNC_CREATE_PATH_REQ:
      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done2);
      path = (char *)mo_vaddr;

      /* Update pathname for namespace */
      ns_id = get_ns_id_from_badge(sender_badge);
      char global_pathname[PATH_MAX];
      strncpy(global_pathname, path, PATH_MAX);

      ns = find_ns(ns_id);
      if (ns == NULL)
      {
        XV6FS_PRINTF("Namespace did not exist\n");
        error = RS_ERROR_NS;
        goto done2;
      }

      apply_prefix(ns->ns_prefix, global_pathname);

      XV6FS_PRINTF("Got request to create the path %s\n", global_pathname);

      /* Create the path in FS */
      error = xv6fs_sys_pathcreate(global_pathname);
      if (error != 0)
      {
        // This may happen if the filepath already exists
        XV6FS_PRINTF("Failed to create the path %s\n", global_pathname);
        error = FS_SERVER_ERROR_UNKNOWN;
        goto done2;
      }

      /* Bookkeep the path */

      // In global NS
      path_id = get_xv6fs_server()->n_filepaths + 1;
      reg_entry = malloc(sizeof(filepath_registry_entry_t));
      reg_entry->nsid = NSID_DEFAULT;
      reg_entry->id = path_id;
      strncpy(reg_entry->path, global_pathname, PATH_MAX);
      filepath_registry_insert(reg_entry);

      filepath_registry_entry_t *global_path_entry = reg_entry;

      if (ns_id != NSID_DEFAULT)
      {
        // And in local NS
        path_id = get_xv6fs_server()->n_filepaths;
        reg_entry = malloc(sizeof(filepath_registry_entry_t));
        reg_entry->nsid = ns_id;
        reg_entry->id = path_id;
        strncpy(reg_entry->path, pathname, PATH_MAX);
        reg_entry->global_path = global_path_entry;
        filepath_registry_insert(reg_entry);
      }

      // Create the resource endpoint
      error = resource_server_create_resource(&get_xv6fs_server()->gen,
                                              get_xv6fs_server()->path_manager_id,
                                              path_id);
      CHECK_ERROR_GOTO(error, "Failed to create the resource", error, done2);

      error = resource_server_give_resource(&get_xv6fs_server()->gen,
                                            get_xv6fs_server()->path_manager_id,
                                            get_ns_id_from_badge(sender_badge),
                                            path_id,
                                            get_client_id_from_badge(sender_badge),
                                            &dest);
      CHECK_ERROR_GOTO(error, "Failed to give the resource", error, done2);

      // Send the reply
      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_CREATE_PATH_ACK_END);
      seL4_SetMR(FSMSGREG_CREATE_PATH_ACK_DEST, dest);
      seL4_SetMR(RSMSGREG_FUNC, FS_FUNC_CREATE_PATH_ACK);
      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid filepath op on badged ep without obj id", error, done2);
    }
  }
  else
  {
    /* Handle Request On Specific Resource */
    XV6FS_PRINTF("Received badged filepath request with object id\n");

    path_id = get_object_id_from_badge(sender_badge);

    // Find the resource
    filepath_registry_entry_t *reg_entry = filepath_registry_get_entry_by_id(path_id);
    if (reg_entry == NULL)
    {
      XV6FS_PRINTF("Received invalid resource for RR request, ID is 0x%lx, local ID is 0x%lx\n",
                   path_id, get_local_object_id_from_badge(path_id));
      error = RS_ERROR_DNE;
      goto done2;
    }

    char *global_pathname = filepath_get_global_path(reg_entry);

    XV6FS_PRINTF("Got request for filepath %s from ns %ld\n", global_pathname, get_ns_id_from_badge(sender_badge));

    switch (op)
    {
    case FS_FUNC_IS_LINKED_REQ:
      // Check if this pathname is linked to a physical file
      bool result = filepath_get_file(reg_entry) != NULL;

      // Send the reply
      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_IS_LINKED_ACK_END);
      seL4_SetMR(FSMSGREG_IS_LINKED_ACK_RES, result);
      seL4_SetMR(RSMSGREG_FUNC, FS_FUNC_IS_LINKED_ACK);
      break;
    case FS_FUNC_LINK_REQ:
      /* Find the file to link */
      seL4_Word file_badge = seL4_GetBadge(0);

      file_entry = file_registry_get_entry_by_badge(file_badge);
      if (file_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid file to link\n");
        error = FS_SERVER_ERROR_BADGE;
        goto done2;
      }

      XV6FS_PRINTF("File to link has id %ld, linking to path %s\n", file_entry->file->id, global_pathname);

      /* Do the link */
      error = xv6fs_sys_dolink2(file_entry->file, global_pathname);

      /* Bookkeep */
      if (reg_entry->nsid == NSID_DEFAULT)
      {
        reg_entry->file = file_entry;
      }
      else
      {
        reg_entry->global_path->file = file_entry;
      }

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_LINK_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_LINK_ACK);
      break;
    case FS_FUNC_UNLINK_REQ:
      XV6FS_PRINTF("Unlink pathname %s\n", global_pathname);
      error = xv6fs_sys_unlink(global_pathname);

      // (XXX) Arya: Cleanup resources, bookkeep

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_UNLINK_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_UNLINK_ACK);
      break;
    case FS_FUNC_READ_REQ:
      int n_bytes_to_read = seL4_GetMR(FSMSGREG_READ_REQ_N);
      int offset = seL4_GetMR(FSMSGREG_READ_REQ_OFFSET);

      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done2);

      // Perform file read
      int n_bytes_ret = xv6fs_sys_read(filepath_get_file(reg_entry)->file, mo_vaddr, n_bytes_to_read, offset);
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
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done2);

      // Perform file write
      n_bytes_ret = xv6fs_sys_write(filepath_get_file(reg_entry)->file, mo_vaddr, n_bytes_to_read, offset);
      XV6FS_PRINTF("Wrote %d bytes to file\n", n_bytes_ret);

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_WRITE_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_WRITE_ACK);
      seL4_SetMR(FSMSGREG_WRITE_ACK_N, n_bytes_ret);
      break;
    case FS_FUNC_CLOSE_REQ:
        /* Cleanup resources associated with the file */;
      file_entry = filepath_get_file(reg_entry);
      file_entry->count--;
      if (file_entry->count <= 0)
      {
        XV6FS_PRINTF("Removing registry entry for file with 0 refcount\n");
        file_registry_remove(file_entry);

        // (XXX) Arya: Do we actually want to remove the registry entry?
      }

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_CLOSE_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_CLOSE_ACK);
      break;
    case FS_FUNC_STAT_REQ:
      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done2);

      /* Call function stat */
      error = xv6fs_sys_stat(filepath_get_file(reg_entry)->file, (struct stat *)mo_vaddr);

      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_STAT_ACK_END);
      seL4_SetMR(RDMSGREG_FUNC, FS_FUNC_STAT_ACK);
      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid filepath op on badged ep with obj id", FS_SERVER_ERROR_UNKNOWN, done2);
    }
  }

done2:
  seL4_MessageInfo_ptr_set_label(&reply_tag, error);
  return reply_tag;
}

static seL4_MessageInfo_t xv6fs_file_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap)
{
  int error = seL4_NoError;
  void *mo_vaddr;
  fs_namespace_t *ns;
  file_registry_entry_t *reg_entry;
  filepath_registry_entry_t *path_entry;
  seL4_CPtr dest;

  unsigned int op = seL4_GetMR(FSMSGREG_FUNC);
  uint64_t obj_id = get_object_id_from_badge(sender_badge);

  seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

  if (obj_id == BADGE_OBJ_ID_NULL)
  { /* Handle Request Not Associated to Object */
    XV6FS_PRINTF("Received badged request with no object id\n");

    char *pathname;

    switch (op)
    {
    case FS_FUNC_CREATE_FILE_REQ:
      int open_flags = seL4_GetMR(FSMSGREG_CREATE_FILE_REQ_FLAGS);

      // Cap contains the pathname to link to
      seL4_Word path_badge = seL4_GetBadge(0);

      XV6FS_PRINTF("Got request to create file with path badge %lx\n", path_badge)
      path_entry = filepath_registry_get_entry_by_badge(path_badge);
      if (path_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid resource for RR request, ID is 0x%lx, local ID is 0x%lx\n",
                     get_object_id_from_badge(path_badge), get_local_object_id_from_badge(path_badge));
        error = RS_ERROR_DNE;
        goto done3;
      }

      pathname = filepath_get_global_path(path_entry);

      // Create the file
      XV6FS_PRINTF("Server opening file %s, flags 0x%x\n", pathname, open_flags);

      struct file *file = xv6fs_sys_open(pathname, open_flags);
      error = file == NULL ? FS_SERVER_ERROR_UNKNOWN : FS_SERVER_NOERROR;
      if (error != 0)
      {
        // Don't announce failed to open files as error, not usually an error
        // (XXX) Arya: This may no longer be true, caught at the client level?
        XV6FS_PRINTF("File did not exist\n");
        error = FS_SERVER_ERROR_NOFILE;
        goto done3;
      }

      XV6FS_PRINTF("Opened file\n");

      // Add to registry if not already present
      file_registry_entry_t *reg_entry = file_registry_get_entry_by_id(file->id);

      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("File not previously open, make new registry entry\n");
        reg_entry = malloc(sizeof(file_registry_entry_t));
        reg_entry->count = 1;
        reg_entry->file = file;
        file_registry_insert(reg_entry);

        // Notify the PD component about the new reousrce
        error = resource_server_create_resource(&get_xv6fs_server()->gen, get_xv6fs_server()->file_manager_id, file->id);
        CHECK_ERROR_GOTO(error, "Failed to create the resource", error, done3);
      }
      else
      {
        XV6FS_PRINTF("File was already open, use previous registry entry\n");
        reg_entry->count++;
        free(file); // We don't need another copy of the structure
        file = reg_entry->file;
      }

      // Create the resource endpoint
      error = resource_server_give_resource(&get_xv6fs_server()->gen,
                                            get_xv6fs_server()->file_manager_id,
                                            get_ns_id_from_badge(sender_badge),
                                            file->id,
                                            get_client_id_from_badge(sender_badge),
                                            &dest);
      CHECK_ERROR_GOTO(error, "Failed to give the resource", error, done3);

      // Link path to file
      if (path_entry->nsid == NSID_DEFAULT)
      {
        path_entry->file = reg_entry;
      }
      else
      {
        path_entry->global_path->file = reg_entry;
      }

      // Send the reply
      seL4_MessageInfo_ptr_set_length(&reply_tag, FSMSGREG_CREATE_FILE_ACK_END);
      seL4_SetMR(FSMSGREG_CREATE_FILE_ACK_DEST, dest);
      seL4_SetMR(RSMSGREG_FUNC, FS_FUNC_CREATE_FILE_ACK);
      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid file op on badged ep without obj id", error, done3);
    }
  }
  else
  {
    /* Handle Request On Specific Resource */
    XV6FS_PRINTF("Received badged request with object id\n");

    int ret;
    file_registry_entry_t *reg_entry = file_registry_get_entry_by_badge(sender_badge);
    if (reg_entry == NULL)
    {
      XV6FS_PRINTF("Received invalid badge\n");
      error = FS_SERVER_ERROR_BADGE;
      goto done3;
    }

    XV6FS_PRINTF("Got request for file with id %ld\n", reg_entry->file->id);

    switch (op)
    {
    default:
      CHECK_ERROR_GOTO(1, "got invalid file op on badged ep with obj id", FS_SERVER_ERROR_UNKNOWN, done3);
    }
  }

done3:
  seL4_MessageInfo_ptr_set_label(&reply_tag, error);
  return reply_tag;
}

seL4_MessageInfo_t xv6fs_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap)
{
  if (sender_badge == 0)
  {
    return xv6fs_untyped_request_handler(tag, sender_badge, cap);
  }
  else
  {
    gpi_cap_t resource_type = get_cap_type_from_badge(sender_badge);

    if (resource_type == GPICAP_TYPE_FILEPATH)
    {
      return xv6fs_filepath_request_handler(tag, sender_badge, cap);
    }
    else if (resource_type == GPICAP_TYPE_FILE)
    {
      return xv6fs_file_request_handler(tag, sender_badge, cap);
    }
    else
    {
      XV6FS_PRINTF("Received message on endpoint with invalid resource type %d\n", resource_type);
      seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(FS_SERVER_ERROR_BADGE, 0, 0, 0);
      return reply_tag;
    }
  }
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
