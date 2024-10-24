
/**
 * @file API for a client process to communicate with the ramdisk server
 */

#pragma once

#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/types.h>

#include <kvstore_shared.h>

typedef enum _kvstore_mode
{
    SAME_THREAD,
    SEPARATE_ADS,
    SEPARATE_THREAD,
    SEPARATE_PROC
} kvstore_mode_t;

/**
 * Configure the kvstore client
 *
 * @param kvstore_mode
 *  - SAME_THREAD: uses a process-local kvstore in the same thread as the caller
 *  - SEPARATE_ADS: server uses the same thread as the caller,
 *                  compartmentalizes client and server heaps in separate ADS
 *  - SEPARATE_THREAD: uses a process-local kvstore in a different thread from the caller
 *  - SEPARATE_PROC: uses a kvstore in a remote process (ep argument is required)
 * @param ep endpoint of the kvstore server (optional)
 * @return 0 on success, seL4 error otherwise
 */
int kvstore_client_configure(kvstore_mode_t kvstore_mode, seL4_CPtr ep);

/**
 * KV store supports simple set and get functions
 * The key and value are each an seL4_Word
 */

/**
 * Create a kvstore table
 * 
 * @param dest returns the location of the created store, if using a remote kvstore
 * @param store_id returns the ID of the create store, if using a local kvstore
 */
int kvstore_client_create_kvstore(seL4_CPtr *dest, gpi_obj_id_t *store_id);

/**
 * @brief Put a key-value pair in the kv store
 * Overwrites any previous value stored for the key
 *
 * @param kvstore_ep endpoint of a particular kvstore resource, if using a remote kvstore
 * @param store_id ID of a particular kvstore, if using a local kvstore
 * @param key Key to store
 * @param value Value to store
 * @return 0 on success, seL4 error otherwise
 */
int kvstore_client_set(seL4_CPtr kvstore_ep, gpi_obj_id_t store_id, seL4_Word key, seL4_Word value);

/**
 * @brief Get a value from the kv store
 *
 * @param kvstore_ep endpoint of a particular kvstore resource, if using a remote kvstore
 * @param store_id ID of a particular kvstore, if using a local kvstore
 * @param key Key to search for
 * @param value Returns the value if the key is found
 * @return 0 on success,
 *         KvstoreError_KEY if the key does not exist,
 *         seL4 error otherwise
 */
int kvstore_client_get(seL4_CPtr kvstore_ep, gpi_obj_id_t store_id, seL4_Word key, seL4_Word *value);

int kvstore_client_swap_ads_lib(void);
int kvstore_client_swap_ads_app(void);