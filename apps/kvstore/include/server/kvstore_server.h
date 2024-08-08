
/**
 * @file API for a client process to communicate with the ramdisk server
 */

#pragma once

#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/types.h>
#include <sel4gpi/resource_server_utils.h>
#include <kvstore_shared.h>

/**
 * KV store supports simple set and get functions
 * The key and value are each an seL4_Word
 */

#define KVSTORE_S "KVstore Server: "

#if KVSTORE_DEBUG
#define KVSTORE_PRINTF(...)       \
    do                            \
    {                             \
        printf("%s ", KVSTORE_S); \
        printf(__VA_ARGS__);      \
    } while (0);
#else
#define KVSTORE_PRINTF(...)
#endif

/*
Context of the server
*/
typedef struct _kvstore_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

    // (XXX) Arya: KVstore server currently only supports one kvstore
    gpi_obj_id_t kvstore_obj_id;
    char db_filename[128]; ///< Name of the file storing the database
} kvstore_server_context_t;

/**
 * Initial setup for kvstore server
*/
int kvstore_server_init();

/**
 * Start the kvstore server as a thread
*/
int kvstore_server_start_thread(seL4_CPtr *kvstore_ep);

/**
 * Main function to serve kvstore requests
*/
int kvstore_server_main(seL4_CPtr parent_ep, gpi_obj_id_t parent_pd_id);

/**
 * @brief Put a key-value pair in the kv store
 * Overwrites any previous value stored for the key
 *
 * @param key Key to store
 * @param value Value to store
 * @return 0 on success, seL4 error otherwise
 */
int kvstore_server_set(seL4_Word key, seL4_Word value);

/**
 * @brief Get a value from the kv store
 *
 * @param key Key to search for
 * @param value Returns the value if the key is found
 * @return 0 on success,
 *         KvstoreError_KEY if the key does not exist,
 *         seL4 error otherwise
 */
int kvstore_server_get(seL4_Word key, seL4_Word *value);