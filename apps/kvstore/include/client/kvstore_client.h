
/**
 * @file API for a client process to communicate with the ramdisk server
 */

#pragma once

#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/types.h>

#include <kvstore_shared.h>

/**
 * Configure the kvstore client
 * 
 * @param use_remote_server if true, forwards kvstore requests to the kvstore server
 *                          if false, uses a process-local kvstore
 * @param ep endpoint of the kvstore server (optional)
 * @return 0 on success, seL4 error otherwise
*/
int kvstore_client_configure(bool use_remote_server, seL4_CPtr ep);

/**
 * KV store supports simple set and get functions
 * The key and value are each an seL4_Word
 */

/**
 * @brief Put a key-value pair in the kv store
 * Overwrites any previous value stored for the key
 *
 * @param key Key to store
 * @param value Value to store
 * @return 0 on success, seL4 error otherwise
 */
int kvstore_client_set(seL4_Word key, seL4_Word value);

/**
 * @brief Get a value from the kv store
 *
 * @param key Key to search for
 * @param value Returns the value if the key is found
 * @return 0 on success,
 *         KVSTORE_ERROR_KEY if the key does not exist,
 *         seL4 error otherwise
 */
int kvstore_client_get(seL4_Word key, seL4_Word *value);