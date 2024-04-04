/**
 * @file xv6fs.h
 * @author
 * @brief Implements functions needed by a parent to interact with the xv6fs server.
 * @version 0.1
 * @date 2024-01-25
 */

#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>
#include <ramdisk_client.h>

#include <fs_shared.h>

#define XV6FS_S "xv6fs Server: "
#define XV6FS_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

struct _ads_client_context;
typedef struct _ads_client_context ads_client_context_t;

struct _pd_client_context;
typedef struct _pd_client_context pd_client_context_t;

/* Per-client context maintained by the server. */
typedef struct _file_registry_entry
{
    struct file *file;
    uint32_t count; // There can be more than one cap to this object
    struct _file_registry_entry *next;

} file_registry_entry_t;

/* Per-client context maintained by the server. */
typedef struct _filepath_registry_entry
{
    uint64_t nsid; // NS this filepath is from
    uint64_t id;   // ID of this filepath resource

    struct _filepath_registry_entry *global_path; // Filepath in NS depends on global filepath
    file_registry_entry_t *file;            // Filepath in global NS depends on file

    char path[MAXPATH];
    uint32_t count; // There can be more than one cap to this object
    struct _filepath_registry_entry *next;
} filepath_registry_entry_t;

/**
 * State maintained for namespaces
 */
typedef struct _fs_namespace
{
    uint64_t id;             // ID of the namespace
    char ns_prefix[MAXPATH]; // prefix of this namespace in the default ns

    struct _fs_namespace *next;
} fs_namespace_t;

/*
Context of the server
*/
typedef struct _xv6fs_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

    // Manager IDs
    uint64_t file_manager_id;
    uint64_t path_manager_id;

    // Other EPs
    seL4_CPtr rd_ep;

    /* Internal data */
    int n_files; // Number of files in file registry
    file_registry_entry_t *file_registry;
    int n_filepaths; // Number of filepaths in filepath registry
    filepath_registry_entry_t *filepath_registry;
    fs_namespace_t *namespaces;

    // Fields for naive block implementation
    mo_client_context_t *shared_mem;
    void *shared_mem_vaddr;
    ramdisk_client_context_t naive_blocks[FS_SIZE];
} xv6fs_server_context_t;

/**
 * To be run once at the start of the fs server
 */
int xv6fs_init();

/**
 * To handle client requests to the fs server
 */
seL4_MessageInfo_t xv6fs_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap);

xv6fs_server_context_t *get_xv6fs_server(void);