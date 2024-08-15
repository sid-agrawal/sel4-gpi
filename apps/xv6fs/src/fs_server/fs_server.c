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
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/resource_space_clientapi.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/gpi_rpc.h>
#include <fs_rpc.pb.h>

#include <ramdisk_client.h>
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
static void make_ns_prefix(char *prefix, gpi_space_id_t nsid)
{
  snprintf(prefix, MAXPATH, "/ns%u", nsid);
}

/**
 * Add the prefix to a path, overwriting path
 */
static void apply_prefix(char *prefix, char *path)
{
  char temp[MAXPATH];

  if (strlen(path) > 0 && path[0] == '/')
  {
    // Don't need to add path separator
    snprintf(temp, MAXPATH, "%s%s", prefix, path);
  }
  else
  {
    snprintf(temp, MAXPATH, "%s/%s", prefix, path);
  }

  strcpy(path, temp);
}

static void ns_registry_entry_on_delete(resource_registry_node_t *node_gen, void *arg)
{
  fs_namespace_entry_t *node = (fs_namespace_entry_t *)node_gen;

  // Cleanup for NS is already done at this point
}

static void file_registry_entry_on_delete(resource_registry_node_t *node_gen, void *arg)
{
  file_registry_entry_t *node = (file_registry_entry_t *)node_gen;

  // Close the file in the FS
  xv6fs_sys_fileclose(node->file);

  // Delete the resource from the global file space
  int error;
  gpi_obj_id_t inum = (gpi_obj_id_t)node->gen.object_id;
  error = resspc_client_delete_resource(&get_xv6fs_server()->gen.default_space, inum);
  CHECK_ERROR_GOTO(error, "Failed to delete file resource in global namespace", FsError_UNKNOWN, err_goto);

  // Delete the file from any namespaces
  resource_registry_node_t *curr, *tmp;
  HASH_ITER(hh, get_xv6fs_server()->ns_registry.head, curr, tmp)
  {
    fs_namespace_entry_t *node = (fs_namespace_entry_t *)curr;

    // We do not track whether or not files exist in a namespace, so we just try deleting from all
    error = resspc_client_delete_resource(&node->res_space_conn, inum);
    CHECK_ERROR_GOTO(error, "Failed to delete file resource in namespace", FsError_UNKNOWN, err_goto);
  }

  return;

err_goto:
  ZF_LOGF("xv6fs Server: Failed to delete file %u\n", inum);
}

/*--- XV6FS SERVER ---*/
static xv6fs_server_context_t xv6fs_server;

xv6fs_server_context_t *get_xv6fs_server(void)
{
  return &xv6fs_server;
}

/**
 * Initializes the file system by requesting
 * every block ahead of time, and using them later for read/write requests
 */
static int init_blocks()
{
  int error;
  seL4_CPtr ramdisk_ep = get_xv6fs_server()->rd_ep;

  for (int i = 0; i < FS_SIZE; i++)
  {
    error = ramdisk_client_alloc_block(ramdisk_ep,
                                       &get_xv6fs_server()->blocks[i]);
    CHECK_ERROR(error, "failed to alloc a block from ramdisk");
  }

  return 0;
}

/**
 * Destroy a namespace from the file system
 * This removes the namespace from the bookkeeping,
 * unlinks all files within the namespace, and removes the directory
 *
 * @param ns_id the ID of the namespace to remove
 * @param notify_rt if true, deletes the resource space from the root task as well
 * @return 0 on success, error otherwises
 */
static int destroy_ns(gpi_space_id_t ns_id, bool notify_rt)
{
  int error = 0;

  // Namespaces are a bit janky at the moment, we don't actually have resources within them,
  // but just delete the corresponding directory

  // Find the namespace from the registry
  fs_namespace_entry_t *ns = (fs_namespace_entry_t *)resource_registry_get_by_id(
      &get_xv6fs_server()->ns_registry,
      ns_id);
  CHECK_ERROR_GOTO(ns == NULL, "Namespace did not exist\n", FsError_NO_NS, err_goto);

  // Clear the namespace's directory
  error = xv6fs_sys_rmdir(ns->ns_prefix, true);
  CHECK_ERROR_GOTO(error, "Failed to delete namespace's directory\n", FsError_UNKNOWN, err_goto);

  // Notify the rt, if applicable
  if (notify_rt)
  {
    // The RT will also delete any resources in the NS from client PDs
    error = resspc_client_destroy(&ns->res_space_conn);
    CHECK_ERROR_GOTO(error, "Failed to delete namespace's resource space from RT\n", FsError_UNKNOWN, err_goto);
  }

  // Clear then namespace from the registry
  resource_registry_delete(&get_xv6fs_server()->ns_registry, (resource_registry_node_t *)ns);

err_goto:
  return error;
}

/**
 * Remove a deleted file from the registry and revoke from all namespaces
 */
static int delete_file(gpi_obj_id_t inum)
{
  int error = 0;

  // Find the file in the global file registry
  uint64_t inum_64 = inum & 0xFFFFFFFF;
  file_registry_entry_t *reg_entry = (file_registry_entry_t *)resource_registry_get_by_id(
      &get_xv6fs_server()->file_registry, inum_64);

  if (reg_entry)
  {
    // Remove the file from registry
    resource_registry_delete(&get_xv6fs_server()->file_registry, (resource_registry_node_t *)reg_entry);
  }

  // If there is no reg entry, then there is no resource to delete

err_goto:
  return error;
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

  error = vmr_client_attach_no_reserve(server->gen.vmr_rde,
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
  error = init_blocks();
  CHECK_ERROR(error, "failed to initialize the blocks");

  /* Initialize the fs */
  error = init_disk_file();
  CHECK_ERROR(error, "failed to initialize disk file");
  binit();
  fsinit(ROOTDEV);

  /* Initialize the registries */
  resource_registry_initialize(&get_xv6fs_server()->file_registry, file_registry_entry_on_delete,
                               NULL, BADGE_OBJ_ID_NULL - 1);
  resource_registry_initialize(&get_xv6fs_server()->ns_registry, ns_registry_entry_on_delete,
                               NULL, BADGE_SPACE_ID_NULL - 1);

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

  CHECK_ERROR_GOTO(msg->magic != FS_RPC_MAGIC,
                   "FS server received message with incorrect magic number\n",
                   FsError_UNKNOWN,
                   done);

  // Get info from badge
  gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);
  gpi_obj_id_t obj_id = get_object_id_from_badge(sender_badge);
  gpi_space_id_t ns_id = get_space_id_from_badge(sender_badge);
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
      gpi_space_id_t ns_id;

      // Register a new resource space for the NS
      error = resource_server_new_res_space(&get_xv6fs_server()->gen,
                                            get_client_id_from_badge(sender_badge), &resspc_conn);
      CHECK_ERROR_GOTO(error, "Failed to create a new resource space for namespace\n", FsError_UNKNOWN, done);
      XV6FS_PRINTF("Registered new namespace with ID %u\n", resspc_conn.id);
      ns_id = resspc_conn.id;

      // The namespace maps to the file space
      error = resspc_client_map_space(&resspc_conn, get_xv6fs_server()->gen.default_space.id);

      // Bookkeeping the NS
      fs_namespace_entry_t *ns_entry = malloc(sizeof(fs_namespace_entry_t));
      CHECK_ERROR_GOTO(ns_entry == NULL, "Failed to malloc a new namespace struct\n", FsError_UNKNOWN, done);
      ns_entry->gen.object_id = ns_id;
      ns_entry->res_space_conn = resspc_conn;
      make_ns_prefix(ns_entry->ns_prefix, resspc_conn.id);
      resource_registry_insert(&get_xv6fs_server()->ns_registry, (resource_registry_node_t *)ns_entry);

      // Create directory in global FS
      error = xv6fs_sys_mkdir(ns_entry->ns_prefix);
      CHECK_ERROR_GOTO(error, "Failed to make new directory for namespace\n", FsError_UNKNOWN, done);

      // Set the reply
      reply_msg->which_msg = FsReturnMessage_ns_tag;
      reply_msg->msg.ns.space_id = ns_id;
      break;
    case FsMessage_create_tag:
      int open_flags = msg->msg.create.flags;
      pathname = msg->msg.create.path;

      /* Update pathname if within a namespace */
      resspc_client_context_t *space_conn = &get_xv6fs_server()->gen.default_space;
      ns_id = get_space_id_from_badge(sender_badge);
      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        fs_namespace_entry_t *ns = (fs_namespace_entry_t *)resource_registry_get_by_id(&get_xv6fs_server()->ns_registry, ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist\n");
          error = FsError_NO_NS;
          goto done;
        }

        space_conn = &ns->res_space_conn;
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
      file_registry_entry_t *reg_entry = (file_registry_entry_t *)resource_registry_get_by_id(&get_xv6fs_server()->file_registry, file->id);

      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("File not previously open, make new registry entry\n");
        reg_entry = malloc(sizeof(file_registry_entry_t));
        CHECK_ERROR_GOTO(reg_entry == NULL, "Failed to malloc a new registry entry\n", FsError_UNKNOWN, done);
        reg_entry->gen.object_id = file->id;
        reg_entry->file = file;
        resource_registry_insert(&get_xv6fs_server()->file_registry, (resource_registry_node_t *)reg_entry);
      }
      else
      {
        XV6FS_PRINTF("File was already open, use previous registry entry\n");
        resource_registry_inc(&get_xv6fs_server()->file_registry, (resource_registry_node_t *)reg_entry);

        xv6fs_sys_fileclose(file); // We don't need another copy of the structure
        file = reg_entry->file;
        filedup(file);
      }

      // Some weirdness due to having namespaces of files instead of file objects
      // Always create the resource, since it may not already exist within the namespace,
      // even if it exists in another namespace

      // Notify the PD component about the new file in the global namespace
      error = resource_server_create_resource(&get_xv6fs_server()->gen, &get_xv6fs_server()->gen.default_space, file->id);
      CHECK_ERROR_GOTO(error, "Failed to create a file resource in the global namespace", error, done);

      // Notify the PD component about the new resource in the namespace
      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        error = resource_server_create_resource(&get_xv6fs_server()->gen, space_conn, file->id);
        CHECK_ERROR_GOTO(error, "Failed to create a file resource in the local namespace", error, done);
      }

#if FS_DEBUG_ENABLED
      // Prints the FS contents to console for debug
      int n_files;
      gpi_obj_id_t inums[16];
      error = xv6fs_sys_walk(ROOT_DIR, true, inums, &n_files);
      CHECK_ERROR_GOTO(error, "Failed to walk FS", FsError_UNKNOWN, done);
#endif

      // Create the resource endpoint
      seL4_CPtr dest;
      error = resource_server_give_resource(&get_xv6fs_server()->gen,
                                            space_conn->id,
                                            // get_xv6fs_server()->gen.default_space.id,
                                            file->id,
                                            get_client_id_from_badge(sender_badge),
                                            &dest);
      CHECK_ERROR_GOTO(error, "Failed to give the resource", error, done);

      // Set the reply
      reply_msg->which_msg = FsReturnMessage_create_tag;
      reply_msg->msg.create.slot = dest;
      break;
    case FsMessage_link_tag:
      CHECK_ERROR_GOTO(!sel4gpi_rpc_check_cap(get_xv6fs_server()->gen.resource_type),
                       "Did not receive FILE cap\n",
                       FsError_BADGE,
                       done);

      seL4_Word file_badge = seL4_GetBadge(0);
      pathname = msg->msg.link.path;

      /* Update pathname if within a namespace */
      ns_id = get_space_id_from_badge(sender_badge);
      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        fs_namespace_entry_t *ns = (fs_namespace_entry_t *)resource_registry_get_by_id(&get_xv6fs_server()->ns_registry, ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist\n");
          error = FsError_NO_NS;
          goto done;
        }

        apply_prefix(ns->ns_prefix, pathname);
      }

      /* Find the file to link */
      reg_entry = (file_registry_entry_t *)resource_registry_get_by_badge(&get_xv6fs_server()->file_registry, file_badge);

      if (reg_entry == NULL)
      {
        XV6FS_PRINTF("Received invalid file to link, number (%u)\n", get_object_id_from_badge(file_badge));
        error = FsError_BADGE;
        goto done;
      }

      XV6FS_PRINTF("File to link has id %lu, linking to path %s\n", reg_entry->gen.object_id, pathname);

      /* Do the link */
      error = xv6fs_sys_dolink2(reg_entry->file, pathname);
      CHECK_ERROR_GOTO(error, "Failed to link", FsError_UNKNOWN, done);

      break;
    case FsMessage_unlink_tag:
      pathname = msg->msg.unlink.path;

      /* Update pathname if within a namespace */
      fs_namespace_entry_t *ns;
      ns_id = get_space_id_from_badge(sender_badge);
      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        ns = (fs_namespace_entry_t *)resource_registry_get_by_id(&get_xv6fs_server()->ns_registry, ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist\n");
          error = FsError_NO_NS;
          goto done;
        }

        apply_prefix(ns->ns_prefix, pathname);
      }

      XV6FS_PRINTF("Unlink pathname %s\n", pathname);
      gpi_obj_id_t inum;
      bool was_last_link;
      error = xv6fs_sys_unlink(pathname, &inum, &was_last_link);
      CHECK_ERROR_GOTO(error, "Failed to unlink", FsError_UNKNOWN, done);

      // Delete the namespace resource, if there is one
      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        error = resspc_client_delete_resource(&ns->res_space_conn, inum);
        CHECK_ERROR_GOTO(error, "Failed to delete file resource in namespace", FsError_UNKNOWN, done);
      }

      // If the unlink deleted the file, delete the file resource
      if (was_last_link)
      {
        error = delete_file(inum);
        CHECK_ERROR_GOTO(error, "Failed to delete file", FsError_UNKNOWN, done);
      }

      break;
    case FsMessage_delete_ns_tag:
      ns_id = get_space_id_from_badge(sender_badge);

      error = destroy_ns(ns_id, true);
      CHECK_ERROR_GOTO(error, "Failed to destroy NS\n", FsError_UNKNOWN, done);

      break;
    default:
      CHECK_ERROR_GOTO(1, "got invalid op on badged ep without obj id", error, done);
    }
  }
  else
  {
    /* Handle Request On Specific Resource */
    XV6FS_PRINTF("Received badged request with object id 0x%u\n", get_object_id_from_badge(sender_badge));

    // Find the file in the registry
    file_registry_entry_t *reg_entry =
        (file_registry_entry_t *)resource_registry_get_by_badge(
            &get_xv6fs_server()->file_registry,
            sender_badge);
    CHECK_ERROR_GOTO(reg_entry == NULL, "Received invalid badge\n", FsError_BADGE, done);

    XV6FS_PRINTF("Got request for file with id %u\n", reg_entry->file->id);
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
      XV6FS_PRINTF("Close file (%u)\n", reg_entry->file->id);

      /* Remove the ref in the registry entry */
      resource_registry_dec(&get_xv6fs_server()->file_registry, (resource_registry_node_t *)reg_entry);

      /* Find the right namespace */
      resspc_client_context_t *space_conn = &get_xv6fs_server()->gen.default_space;

      if (ns_id != get_xv6fs_server()->gen.default_space.id)
      {
        fs_namespace_entry_t *ns = (fs_namespace_entry_t *)
            resource_registry_get_by_id(&get_xv6fs_server()->ns_registry, ns_id);
        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist\n");
          error = FsError_NO_NS;
          goto done;
        }

        space_conn = &ns->res_space_conn;
      }

      /* Remove the resource from the corresponding PD */
      error = resspc_client_revoke_resource(space_conn, obj_id, client_id);
      CHECK_ERROR_GOTO(error, "Failed to revoke closed file", error, done);
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
  int error = ramdisk_client_read(&get_xv6fs_server()->blocks[blockno]);

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
  return ramdisk_client_write(&get_xv6fs_server()->blocks[blockno]);
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
// (XXX) Arya: If we used this, we would need to track unmapping as well
void map_file_to_block(uint32_t file_id, uint32_t blockno)
{
#if TRACK_MAP_RELATIONS
  int error = 0;

  file_registry_entry_t *reg_entry = (file_registry_entry_t *)
      resource_registry_get_by_id(&get_xv6fs_server()->file_registry, file_id);

  if (reg_entry == NULL)
  {
    XV6FS_PRINTF("Warning: did not find file for inode %d\n", file_id);
    return;
  }

  gpi_badge_t file_universal_id = compact_res_id(get_xv6fs_server()->gen.resource_type,
                                                 get_xv6fs_server()->gen.default_space.id, file_id);
  gpi_badge_t block_universal_id = compact_res_id(sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME),
                                                  get_xv6fs_server()->blocks[blockno].space_id,
                                                  get_xv6fs_server()->blocks[blockno].res_id);

  error = pd_client_map_resource(&get_xv6fs_server()->gen.pd_conn, file_universal_id, block_universal_id);
  SERVER_GOTO_IF_ERR(error, "Failed to map file (%u) to block (%u)\n", file_universal_id, block_universal_id);

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
    /* Initialize the model state */
    mo_client_context_t mo;
    model_state_t *model_state;
    error = resource_server_extraction_setup(&get_xv6fs_server()->gen, 4, &mo, &model_state);
    CHECK_ERROR_GOTO(error, "Failed to setup model extraction\n", FsError_UNKNOWN, err_goto);

    for (int i = 0; i < work->object_ids_count; i++)
    {
      gpi_space_id_t space_id = work->space_ids[i];
      gpi_obj_id_t fileno = work->object_ids[i];
      gpi_obj_id_t client_pd_id = work->pd_ids[i];

      if (fileno != BADGE_OBJ_ID_NULL)
      {
        /* File system only does extraction at a space-level, not at a file-level */
        continue;
      }

      /* Update pathname for namespace */
      char path[MAXPATH];
      strcpy(path, ROOT_DIR);

      if (space_id != get_xv6fs_server()->gen.default_space.id)
      {
        fs_namespace_entry_t *ns = (fs_namespace_entry_t *)resource_registry_get_by_id(
            &get_xv6fs_server()->ns_registry,
            space_id);

        if (ns == NULL)
        {
          XV6FS_PRINTF("Namespace did not exist for dumprr\n");
          error = FsError_NO_NS;
          goto err_goto;
        }

        apply_prefix(ns->ns_prefix, path);
      }

      /* List all the files in the NS */
      int n_files;
      gpi_obj_id_t inums[16];

      error = xv6fs_sys_walk(path, false, inums, &n_files);
      CHECK_ERROR_GOTO(error, "Failed to walk FS", FsError_UNKNOWN, err_goto);

      /* Add the PD nodes */
      char client_pd_id_str[CSV_MAX_STRING_SIZE];
      get_pd_id(client_pd_id, client_pd_id_str);
      char fs_pd_id_str[CSV_MAX_STRING_SIZE];
      get_pd_id(sel4gpi_get_pd_conn().id, fs_pd_id_str);

      /* Add the file resource space ID(s) */
      char file_space_id[CSV_MAX_STRING_SIZE];
      get_resource_space_id(get_xv6fs_server()->gen.resource_type,
                            get_xv6fs_server()->gen.default_space.id,
                            file_space_id);

      char file_ns_space_id[CSV_MAX_STRING_SIZE];
      get_resource_space_id(get_xv6fs_server()->gen.resource_type,
                            space_id,
                            file_ns_space_id);

      /* Add nodes for all files and blocks */
      gpi_cap_t block_cap_type = sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME);
      // (XXX) Arya: Assume only one block space
      gpi_space_id_t block_space_id = get_xv6fs_server()->blocks[0].space_id;
      int n_blocknos = 100;
      // (XXX) Arya: assumes there are no more than 100 blocks per file
      int *blocknos = malloc(sizeof(int) * n_blocknos);
      for (int i = 0; i < n_files; i++)
      {
        XV6FS_PRINTF("Get RR for fileno %u\n", inums[i]);

        // We have to add all file resource nodes here, because the files may be closed,
        // so the root task wouldn't add them

        /* Add the file resource node */
        gpi_model_node_t *file_node = add_resource_node(
            model_state,
            make_res_id(get_xv6fs_server()->gen.resource_type, get_xv6fs_server()->gen.default_space.id, inums[i]),
            true);

        /* Add the subset edge */
        add_edge_by_id(model_state, GPI_EDGE_TYPE_SUBSET, file_node->id, file_space_id);

        /* Add the hold edges */
        add_edge_by_id(model_state, GPI_EDGE_TYPE_HOLD, fs_pd_id_str, file_node->id);

        /* If in a namespace, add the file resource node in the namespace */
        if (space_id != get_xv6fs_server()->gen.default_space.id)
        {
          gpi_model_node_t *file_ns_node = add_resource_node(
              model_state,
              make_res_id(get_xv6fs_server()->gen.resource_type, space_id, inums[i]),
              true);

          // Add the subset edge
          add_edge_by_id(model_state, GPI_EDGE_TYPE_SUBSET, file_ns_node->id, file_ns_space_id);

          // Add the map edge to the file in the default file space
          add_edge(model_state, GPI_EDGE_TYPE_MAP, file_ns_node, file_node);

          // FS holds all files
          add_edge_by_id(model_state, GPI_EDGE_TYPE_HOLD, fs_pd_id_str, file_ns_node->id);

          // Client holds the resource in the namespace
          add_edge_by_id(model_state, GPI_EDGE_TYPE_HOLD, client_pd_id_str, file_ns_node->id);
        }
        else
        {
          // Client holds the file directly
          add_edge_by_id(model_state, GPI_EDGE_TYPE_HOLD, client_pd_id_str, file_node->id);
        }

        /* Add relations for blocks */
        error = xv6fs_sys_inode_blocknos(inums[i], blocknos, n_blocknos, &n_blocknos);
        CHECK_ERROR_GOTO(error, "Failed to get blocknos for file", FsError_UNKNOWN, err_goto);
        XV6FS_PRINTF("File has %d blocks\n", n_blocknos);

        gpi_obj_id_t block_id;
        for (int j = 0; j < n_blocknos; j++)
        {
          block_id = get_xv6fs_server()->blocks[blocknos[j]].res_id;

          char block_id_str[CSV_MAX_STRING_SIZE];
          get_resource_id(make_res_id(block_cap_type, block_space_id, block_id), block_id_str);
          add_edge_by_id(model_state, GPI_EDGE_TYPE_MAP, file_node->id, block_id_str);
        }
      }

      free(blocknos);
    }

    /* Send the result */
    error = resource_server_extraction_finish(&get_xv6fs_server()->gen, &mo, model_state, work->object_ids_count);
    CHECK_ERROR_GOTO(error, "Failed to finish model extraction\n", FsError_UNKNOWN, err_goto);
  }
  else if (op == PdWorkAction_FREE)
  {
    for (int i = 0; i < work->object_ids_count; i++)
    {
      // Actually don't do much when a file is freed, it's the same as file close
      // We still want to keep it in the file system, but we can reduce the refcount
      gpi_obj_id_t file_id = work->object_ids[i];

      // Find the registry entry
      file_registry_entry_t *reg_entry = (file_registry_entry_t *)resource_registry_get_by_id(
          &get_xv6fs_server()->file_registry,
          file_id);

      if (reg_entry == NULL)
      {
        // No-op if the file doesn't exist / isn't open
        XV6FS_PRINTF("Received file (%u) to free, file isn't open\n", file_id);
      }
      else
      {
        // Decrement the refcount
        resource_registry_dec(&get_xv6fs_server()->file_registry, (resource_registry_node_t *)reg_entry);
      }
    }

    error = pd_client_finish_work(&get_xv6fs_server()->gen.pd_conn, work->object_ids_count, work->n_critical);
  }
  else if (op == PdWorkAction_DESTROY)
  {
    for (int i = 0; i < work->object_ids_count; i++)
    {
      gpi_obj_id_t file_id = work->object_ids[i];
      gpi_space_id_t space_id = work->space_ids[i];

      if (file_id != BADGE_OBJ_ID_NULL)
      {
        // Destroy a particular file

        // Find the registry entry
        file_registry_entry_t *reg_entry = (file_registry_entry_t *)resource_registry_get_by_id(
            &get_xv6fs_server()->file_registry,
            file_id);

        if (reg_entry == NULL)
        {
          // No-op if the file doesn't exist / isn't open
          XV6FS_PRINTF("Received file (%u) to free, file isn't open\n", file_id);
        }
        else
        {
          // Decrement the refcount
          resource_registry_delete(&get_xv6fs_server()->file_registry, (resource_registry_node_t *)reg_entry);
        }
      }
      else
      {
        // Destroy a whole space
        if (space_id == get_xv6fs_server()->gen.default_space.id)
        {
          // Destroy the entire file system, this is done by releasing the disk

          // Nothing to do if we can't access the disk
          if (sel4gpi_can_request_type(BLOCK_RESOURCE_TYPE_NAME))
          {

            for (int i = 0; i < FS_SIZE; i++)
            {
              error = ramdisk_client_free_block(&get_xv6fs_server()->blocks[i]);

              if (error)
              {
                if (!sel4gpi_can_request_type(BLOCK_RESOURCE_TYPE_NAME))
                {
                  // We got an error because we no longer have access to the ramdisk
                  break;
                }
                else
                {
                  CHECK_ERROR_GOTO(error, "Failed to free block\n", FsError_UNKNOWN, err_goto);
                }
              }
            }
          }
        }
        else
        {
          // Destroy just a namespace from the file system
          error = destroy_ns(space_id, false);
          CHECK_ERROR_GOTO(error, "Failed to destroy NS\n", FsError_UNKNOWN, err_goto);
        }
      }
    }

    error = pd_client_finish_work(&get_xv6fs_server()->gen.pd_conn, work->object_ids_count, work->n_critical);
  }
  else
  {
    XV6FS_PRINTF("Unknown work action\n");
    error = 1;
  }

err_goto:
  return error;
}
