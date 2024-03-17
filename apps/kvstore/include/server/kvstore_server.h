
/**
 * @file API for a client process to communicate with the ramdisk server
 */

#pragma once

#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/types.h>

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

/**
 * Initial setup for kvstore server
*/
int kvstore_server_init();

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
 *         KVSTORE_ERROR_KEY if the key does not exist,
 *         seL4 error otherwise
 */
int kvstore_server_get(seL4_Word key, seL4_Word *value);