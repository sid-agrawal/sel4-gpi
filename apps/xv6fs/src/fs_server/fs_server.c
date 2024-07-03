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

#include <sel4gpi/pd_utils.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_remote_utils.h>
#include <sel4gpi/resource_space_clientapi.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/gpi_rpc.h>
#include <fs_rpc.pb.h>

#include <ramdisk_client.h>
#include <libc_fs_helpers.h>
#include <fs_shared.h>
#include <fs_server.h>
#include <defs.h>
#include <file.h>
#include <buf.h>

#if FS_DEBUG_ENABLED
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

// Defined for utility printing macros
#define DEBUG_ID FS_DEBUG
#define SERVER_ID XV6FS_S

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

static void ns_registry_entry_on_delete(resource_server_registry_node_t *node_gen, void *arg)
{
  fs_namespace_entry_t *node = (fs_namespace_entry_t *)node_gen;

  // (XXX) Any cleanup necessary for NS
}

static void file_registry_entry_on_delete(resource_server_registry_node_t *node_gen, void *arg)
{
  file_registry_entry_t *node = (file_registry_entry_t *)node_gen;

  // Close the file in the FS
  xv6fs_sys_fileclose(node->file);
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

  error = mo_component_client_connect(server->gen.mo_ep,
                                      1,
                                      MO_PAGE_BITS,
                                      server->shared_mem);
  CHECK_ERROR(error, "failed to allocate shared mem page");

  error = ads_client_attach(&server->gen.ads_conn,
                            NULL,
                            server->shared_mem,
                            SEL4UTILS_RES_TYPE_SHARED_FRAMES,
                            &server->shared_mem_vaddr);
  CHECK_ERROR(error, "failed to map shared mem page");

  /* Initialize connection with ramdisk */
  error = ramdisk_client_bind(get_xv6fs_server()->rd_ep, server->shared_mem);
  CHECK_ERROR(error, "failed to bind shared mem page");

  /* Map the file space to the block space */
  gpi_cap_t block_cap_type = sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME);
  error = resspc_client_map_space(&get_xv6fs_server()->gen.default_space,
                                  sel4gpi_get_default_space_id(block_cap_type));

  /* Initialize the blocks */
  error = init_naive_blocks();
  CHECK_ERROR(error, "failed to initialize the blocks");

  /* Initialize the fs */
  error = init_disk_file();
  CHECK_ERROR(error, "failed to initialize disk file");
  binit();
  fsinit(ROOTDEV);

  /* Initialize the registries */
  resource_server_initialize_registry(&get_xv6fs_server()->file_registry, file_registry_entry_on_delete, NULL);
  resource_server_initialize_registry(&get_xv6fs_server()->ns_registry, ns_registry_entry_on_delete, NULL);

  XV6FS_PRINTF("Initialized file system\n");

  return error;
}

void xv6fs_request_handler(void *msg_p,
                           void *msg_reply_p,
                           seL4_Word sender_badge,
                           seL4_CPtr cap, bool *need_new_recv_cap)
{
  int error = 0;
  void *mo_vaddr;
  *need_new_recv_cap = false;
  FsMessage *msg = (FsMessage *)msg_p;
  FsReturnMessage *reply_msg = (FsReturnMessage *)msg_reply_p;
  reply_msg->which_msg = FsReturnMessage_basic_tag;

  // Get info from badge
  uint64_t client_id = get_client_id_from_badge(sender_badge);
  uint64_t obj_id = get_object_id_from_badge(sender_badge);
  uint64_t ns_id = get_space_id_from_badge(sender_badge);
  gpi_cap_t cap_type = get_cap_type_from_badge(sender_badge);

  CHECK_ERROR_GOTO(sender_badge == 0, "Got message on unbadged ep", FsError_UNKNOWN, done);
  CHECK_ERROR_GOTO(cap_type != get_xv6fs_server()->gen.resource_type, "Got invalid captype",
                   FsError_UNKNOWN, done);

  // Handle the message
  if (obj_id == BADGE_OBJ_ID_NULL)
  { /* Handle Request Not Associated to Object */
    XV6FS_PRINTF("Received badged request with no object id\n");

    char *pathname;

    switch (msg->which_msg)
    {
    case FsMessage_ns_tag:
      XV6FS_PRINTF("Got request for new namespace\n");

      resspc_client_context_t resspc_conn;
      uint64_t ns_id;

      // Register a new resource space for the NS
      error = resource_server_new_res_space(&get_xv6fs_server()->gen,
                                            get_client_id_from_badge(sender_badge), &resspc_conn);
      CHECK_ERROR_GOTO(error, "Failed to create a new resource space for namespace\n", FsError_UNKNOWN, done);
      XV6FS_PRINTF("Registered new namespace with ID %ld\n", resspc_conn.id);
      ns_id = resspc_conn.id;

      // Bookkeeping the NS
      fs_namespace_entry_t *ns_entry = malloc(sizeof(fs_namespace_entry_t));
      ns_entry->gen.object_id = ns_id;
      ns_entry->res_space_conn = resspc_conn;
      make_ns_prefix(ns_entry->ns_prefix, resspc_conn.id);
      resource_server_registry_insert(&get_xv6fs_server()->ns_registry, (resource_server_registry_node_t *)ns_entry);

      // Create directory in global FS
      error = xv6fs_sys_mkdir(ns_entry->ns_prefix);
      CHECK_ERROR_GOTO(error, "Failed to make new directory for namespace\n", FsError_UNKNOWN, done);

      // Set the reply
      reply_msg->which_msg = FsReturnMessage_ns_tag;
      reply_msg->msg.ns.space_id = ns_id;
      break;
    case FsMessage_create_tag:
      *need_new_recv_cap = true;

      int open_flags = msg->msg.create.flags;

      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);
      pathname = (char *)mo_vaddr;

      /* Update pathname if within a namespace */
      ns_id = get_space_id_from_badge(sender_badge);
      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        fs_namespace_entry_t *ns = (fs_namespace_entry_t *)resource_server_registry_get_by_id(&get_xv6fs_server()->ns_registry, ns_id);
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
      error = file == NULL ? FsError_UNKNOWN : FsError_NONE;
      if (error != 0)
      {
        // Don't announce failed to open files as error, not usually an error
        XV6FS_PRINTF("File did not exist\n");
        error = FsError_NO_FILE;
        goto done;
      }

      // Add to registry if not already present
      file_registry_entry_t *reg_entry = (file_registry_entry_t *)resource_server_registry_get_by_id(&get_xv6fs_server()->file_registry, file->id);

      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("File not previously open, make new registry entry\n");
        reg_entry = malloc(sizeof(file_registry_entry_t));
        reg_entry->gen.object_id = file->id;
        reg_entry->file = file;
        resource_server_registry_insert(&get_xv6fs_server()->file_registry, (resource_server_registry_node_t *)reg_entry);

        // Notify the PD component about the new reousrce
        error = resource_server_create_resource(&get_xv6fs_server()->gen, NULL, file->id);
        CHECK_ERROR_GOTO(error, "Failed to create the resource", error, done);
      }
      else
      {
        XV6FS_PRINTF("File was already open, use previous registry entry\n");
        resource_server_registry_inc(&get_xv6fs_server()->file_registry, (resource_server_registry_node_t *)reg_entry);

        xv6fs_sys_fileclose(file); // We don't need another copy of the structure
        file = reg_entry->file;
        filedup(file);
      }

#if FS_DEBUG_ENABLED
      // Prints the FS contents to console for debug
      int n_files;
      uint32_t inums[16];
      error = xv6fs_sys_walk(ROOT_DIR, true, inums, &n_files);
      CHECK_ERROR_GOTO(error, "Failed to walk FS", FsError_UNKNOWN, done);
#endif

      // Create the resource endpoint
      // (XXX) Arya: There is only a file object, which belongs to the default space
      // so we have to give the resource in the default space
      // If we add file name resources, they would actually belong to a namespace
      seL4_CPtr dest;
      error = resource_server_give_resource(&get_xv6fs_server()->gen,
                                            // get_space_id_from_badge(sender_badge),
                                            get_xv6fs_server()->gen.default_space.id,
                                            file->id,
                                            get_client_id_from_badge(sender_badge),
                                            &dest);
      CHECK_ERROR_GOTO(error, "Failed to give the resource", error, done);

      // Unattach the MO
      error = resource_server_unattach(&get_xv6fs_server()->gen, mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to unattach MO", error, done);

      // Set the reply
      reply_msg->which_msg = FsReturnMessage_create_tag;
      reply_msg->msg.create.slot = dest;
      break;
    case FsMessage_link_tag:
      *need_new_recv_cap = true;
      CHECK_ERROR_GOTO(!sel4gpi_rpc_check_caps_2(GPICAP_TYPE_NONE, get_xv6fs_server()->gen.resource_type),
                       "Did not receive MO/FILE caps\n",
                       FsError_BADGE,
                       done);

      seL4_Word file_badge = seL4_GetBadge(1);

      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);
      pathname = (char *)mo_vaddr;

      /* Update pathname if within a namespace */
      ns_id = get_space_id_from_badge(sender_badge);
      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        fs_namespace_entry_t *ns = (fs_namespace_entry_t *)resource_server_registry_get_by_id(&get_xv6fs_server()->ns_registry, ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist\n");
          error = RS_ERROR_NS;
          goto done;
        }

        apply_prefix(ns->ns_prefix, pathname);
      }

      /* Find the file to link */
      reg_entry = (file_registry_entry_t *)resource_server_registry_get_by_badge(&get_xv6fs_server()->file_registry, file_badge);

      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid file to link, number (%d)\n", get_object_id_from_badge(file_badge));
        error = FsError_BADGE;
        goto done;
      }

      XV6FS_PRINTF("File to link has id %ld, linking to path %s\n", reg_entry->gen.object_id, pathname);

      /* Do the link */
      error = xv6fs_sys_dolink2(reg_entry->file, pathname);
      CHECK_ERROR_GOTO(error, "Failed to link", FsError_UNKNOWN, done);

      // Unattach the MO
      error = resource_server_unattach(&get_xv6fs_server()->gen, mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to unattach MO", error, done);

      break;
    case FsMessage_unlink_tag:
      *need_new_recv_cap = true;

      /* Attach memory object to server ADS (contains pathname) */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);
      pathname = (char *)mo_vaddr;

      /* Update pathname if within a namespace */
      ns_id = get_space_id_from_badge(sender_badge);
      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        fs_namespace_entry_t *ns = (fs_namespace_entry_t *)resource_server_registry_get_by_id(&get_xv6fs_server()->ns_registry, ns_id);
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
      CHECK_ERROR_GOTO(error, "Failed to unlink", FsError_UNKNOWN, done);

      // Unattach the MO
      error = resource_server_unattach(&get_xv6fs_server()->gen, mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to unattach MO", error, done);

      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid op on badged ep without obj id", error, done);
    }
  }
  else
  {
    /* Handle Request On Specific Resource */
    XV6FS_PRINTF("Received badged request with object id 0x%lx\n", get_object_id_from_badge(sender_badge));

    // Find the file in the registry
    file_registry_entry_t *reg_entry =
        (file_registry_entry_t *)resource_server_registry_get_by_badge(
            &get_xv6fs_server()->file_registry,
            sender_badge);
    CHECK_ERROR_GOTO(reg_entry == NULL, "Received invalid badge\n", FsError_BADGE, done);

    XV6FS_PRINTF("Got request for file with id %ld\n", reg_entry->file->id);
    switch (msg->which_msg)
    {
    case FsMessage_read_tag:
      *need_new_recv_cap = true;

      int n_bytes_to_read = msg->msg.read.n;
      int offset = msg->msg.read.offset;

      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      // Perform file read
      int n_bytes_ret = xv6fs_sys_read(reg_entry->file, mo_vaddr, n_bytes_to_read, offset);
      XV6FS_PRINTF("Read %d bytes from file\n", n_bytes_ret);

      // Unattach the MO
      error = resource_server_unattach(&get_xv6fs_server()->gen, mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to unattach MO", error, done);

      reply_msg->which_msg = FsReturnMessage_read_tag;
      reply_msg->msg.read.n = n_bytes_ret;
      break;
    case FsMessage_write_tag:
      *need_new_recv_cap = true;

      n_bytes_to_read = msg->msg.write.n;
      offset = msg->msg.write.offset;

      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      // Perform file write
      n_bytes_ret = xv6fs_sys_write(reg_entry->file, mo_vaddr, n_bytes_to_read, offset);
      XV6FS_PRINTF("Wrote %d bytes to file\n", n_bytes_ret);

      // Unattach the MO
      error = resource_server_unattach(&get_xv6fs_server()->gen, mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to unattach MO", error, done);

      reply_msg->which_msg = FsReturnMessage_write_tag;
      reply_msg->msg.write.n = n_bytes_ret;
      break;
    case FsMessage_close_tag:
      XV6FS_PRINTF("Close file (%d)\n", reg_entry->file->id);

      /* Remove the ref in the registry entry */
      resource_server_registry_dec(&get_xv6fs_server()->file_registry, (resource_server_registry_node_t *)reg_entry);
      break;
    case FsMessage_stat_tag:
      *need_new_recv_cap = true;

      /* Attach memory object to server ADS */
      error = resource_server_attach_mo(&get_xv6fs_server()->gen, cap, &mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to attach MO", error, done);

      /* Call function stat */
      error = xv6fs_sys_stat(reg_entry->file, (struct stat *)mo_vaddr);

      // Unattach the MO
      error = resource_server_unattach(&get_xv6fs_server()->gen, mo_vaddr);
      CHECK_ERROR_GOTO(error, "Failed to unattach MO", error, done);
      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid op on badged ep with obj id", FsError_UNKNOWN, done);
    }
  }

done:
  reply_msg->errorCode = error;
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
  int error = block_read(blockno, buf);

  if (error)
  {
    XV6FS_PRINTF("Warning: Failed block read\n");
  }
}

void xv6fs_bwrite(uint32_t blockno, void *buf)
{
  int error = block_write(blockno, buf);

  if (error)
  {
    XV6FS_PRINTF("Warning: Failed block write\n");
  }
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

/* Notifies the component when a new block is assigned to a file */
// (XXX) Arya: what about releasing?
void map_file_to_block(uint64_t file_id, uint32_t blockno)
{
#if TRACK_MAP_RELATIONS
  int error = 0;

  file_registry_entry_t *reg_entry = (file_registry_entry_t *)
      resource_server_registry_get_by_id(&get_xv6fs_server()->file_registry, file_id);

  if (reg_entry == NULL)
  {
    XV6FS_PRINTF("Warning: did not find file for inode %d\n", file_id);
    return;
  }

  seL4_Word file_universal_id = universal_res_id(get_xv6fs_server()->gen.resource_type,
                                                 get_xv6fs_server()->gen.default_space.id, file_id);
  seL4_Word block_universal_id = universal_res_id(sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME),
                                                  get_xv6fs_server()->naive_blocks[blockno].space_id,
                                                  get_xv6fs_server()->naive_blocks[blockno].res_id);

  error = pd_client_map_resource(&get_xv6fs_server()->gen.pd_conn, file_universal_id, block_universal_id);
  SERVER_GOTO_IF_ERR(error, "Failed to map file (%lx) to block (%lx)\n", file_universal_id, block_universal_id);

  return;

err_goto:
  ZF_LOGF("Failed to map file to block\n");

#endif
}

int xv6fs_work_handler(PdWorkReturnMessage *work)
{
  int error = 0;
  seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

  int op = work->action;
  if (op == PdWorkAction_EXTRACT)
  {
    uint64_t space_id = work->space_id;
    uint64_t fileno = work->object_id;
    uint64_t client_pd_id = work->pd_id;

    if (fileno != BADGE_OBJ_ID_NULL)
    {
      /* File system only does extraction at a space-level, not at a file-level */
      error = resource_server_extraction_no_data(&get_xv6fs_server()->gen);
      CHECK_ERROR_GOTO(error, "Failed to finish model extraction\n", FsError_UNKNOWN, err_goto);
    }

    /* Initialize the model state */
    mo_client_context_t mo;
    model_state_t *model_state;
    error = resource_server_extraction_setup(&get_xv6fs_server()->gen, 4, &mo, &model_state);
    CHECK_ERROR_GOTO(error, "Failed to setup model extraction\n", FsError_UNKNOWN, err_goto);

    /* Update pathname for namespace */
    char path[MAXPATH];
    strcpy(path, ROOT_DIR);

    if (space_id != get_xv6fs_server()->gen.default_space.id)
    {
      fs_namespace_entry_t *ns = (fs_namespace_entry_t *)resource_server_registry_get_by_id(
          &get_xv6fs_server()->ns_registry,
          space_id);

      if (ns == NULL)
      {
        XV6FS_PRINTF("Namespace did not exist for dumprr\n");
        error = RS_ERROR_NS;
        goto err_goto;
      }

      apply_prefix(ns->ns_prefix, path);
    }

    /* List all the files in the NS */
    int n_files;
    uint32_t inums[16];

    error = xv6fs_sys_walk(path, false, inums, &n_files);
    CHECK_ERROR_GOTO(error, "Failed to walk FS", FsError_UNKNOWN, err_goto);

    // (XXX) Arya: A lot of this should be moved to PD component once we have resource spaces implemented

    /* Add the PD nodes */
    char client_pd_id_str[CSV_MAX_STRING_SIZE];
    get_pd_id(client_pd_id, client_pd_id_str);

    /* Add the file resource space node */
    char file_space_id[CSV_MAX_STRING_SIZE];
    get_resource_space_id(get_xv6fs_server()->gen.resource_type,
                          get_xv6fs_server()->gen.default_space.id,
                          file_space_id);

    /* Add nodes for all files and blocks */
    gpi_cap_t block_cap_type = sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME);
    uint32_t block_space_id = get_xv6fs_server()->naive_blocks[0].space_id; // (XXX) Arya: Assume only one block space
    int n_blocknos = 100;                                                   // (XXX) Arya: assumes there are no more than 100 blocks per file
    int *blocknos = malloc(sizeof(int) * n_blocknos);
    for (int i = 0; i < n_files; i++)
    {
      XV6FS_PRINTF("Get RR for fileno %ld\n", inums[i]);

      /* Add the file resource node */
      char file_id[CSV_MAX_STRING_SIZE];
      get_resource_id(get_xv6fs_server()->gen.resource_type,
                      get_xv6fs_server()->gen.default_space.id,
                      inums[i],
                      file_id);
      add_edge_by_id(model_state, GPI_EDGE_TYPE_HOLD, client_pd_id_str, file_id);

      /* Add relations for blocks */
      error = xv6fs_sys_inode_blocknos(inums[i], blocknos, n_blocknos, &n_blocknos);
      CHECK_ERROR_GOTO(error, "Failed to get blocknos for file", FsError_UNKNOWN, err_goto);
      XV6FS_PRINTF("File has %d blocks\n", n_blocknos);

      uint64_t block_id;
      for (int j = 0; j < n_blocknos; j++)
      {
        block_id = get_xv6fs_server()->naive_blocks[blocknos[j]].res_id;

        char block_id_str[CSV_MAX_STRING_SIZE];
        get_resource_id(block_cap_type, block_space_id, block_id, block_id_str);
        add_edge_by_id(model_state, GPI_EDGE_TYPE_MAP, file_id, block_id_str);
      }
    }

    free(blocknos);

    /* Send the result */
    error = resource_server_extraction_finish(&get_xv6fs_server()->gen, &mo, model_state);
    CHECK_ERROR_GOTO(error, "Failed to finish model extraction\n", FsError_UNKNOWN, err_goto);
  }
  else if (op == PdWorkAction_FREE)
  {
    // Actually don't do much when a file is freed, it's the same as file close
    // We still want to keep it in the file system, but we can reduce the refcount
    uint64_t file_id = work->object_id;

    // Find the registry entry
    file_registry_entry_t *reg_entry = (file_registry_entry_t *)resource_server_registry_get_by_id(
        &get_xv6fs_server()->file_registry,
        file_id);

    if (reg_entry == NULL)
    {
      // No-op if the file doesn't exist / isn't open
      XV6FS_PRINTF("Received file (%d) to free, file isn't open\n");
    }
    else
    {
      // Decrement the refcount
      resource_server_registry_dec(&get_xv6fs_server()->file_registry, (resource_server_registry_node_t *)reg_entry);
    }
  }
  else
  {
    XV6FS_PRINTF("Unknown work action\n");
    error = 1;
  }

err_goto:
  return error;
}
