#pragma once
/**
 * @file badge_usage.h
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief API for a parent to spawn a GPI server.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4gpi/resource_types.h>

#define BADGE_OBJ_ID_NULL 0xfffff
#define MAX_BADGE_STR_SIZE 512

// requires that SERVER_ID and DEBUG_ID are defined
#define BADGE_PRINT(badge)                                                                 \
    do                                                                                     \
    {                                                                                      \
        OSDB_PRINTF_2("BG: %lx\t", (badge));                                               \
        OSDB_PRINTF_2("CapType: %s\t", cap_type_to_str(get_cap_type_from_badge((badge)))); \
        OSDB_PRINTF_2("Perms: %lu\t", get_perms_from_badge((badge)));                      \
        OSDB_PRINTF_2("SpaceID: %lu\t", get_space_id_from_badge((badge)));                 \
        OSDB_PRINTF_2("CID: %lu\t", get_client_id_from_badge((badge)));                    \
        OSDB_PRINTF_2("OID: %lu\n", get_object_id_from_badge((badge)));                    \
    } while (0)

/*
 How we are using the badge.
 There are a total of 64 bits.
63:56  8 bits for the type of cap.
55:48 8 bits for permissions, as a bit mask.
47:40 8 bits for the namespace ID
39:20 20 bits for client ID
16:19 4 bits for server ID
16:0  16 bits for object ID
*/

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
uint64_t get_cap_type_from_badge(seL4_Word badge);

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
uint64_t set_cap_type_to_badge(seL4_Word badge, uint64_t type);

// Bits: 55:40 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
uint64_t get_perms_from_badge(seL4_Word badge);

// Bits: 55:40 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
uint64_t set_perms_to_badge(seL4_Word badge, uint64_t perms);

// Bits: 47:40 are for the resource space ID. Total of 8 bits, so 255 resource spaces
uint64_t get_space_id_from_badge(seL4_Word badge);

// Bits: 47:40 are for the resource space ID. Total of 8 bits, so 255 resource spaces
uint64_t set_space_id_to_badge(seL4_Word badge, uint64_t space_id);

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
uint64_t get_client_id_from_badge(seL4_Word badge);

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
uint64_t set_client_id_to_badge(seL4_Word badge, uint64_t client_id);

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
uint64_t get_object_id_from_badge(seL4_Word badge);

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
uint64_t set_object_id_to_badge(seL4_Word badge, uint64_t object_id);

uint64_t gpi_new_badge(gpi_cap_t cap_type,
                       uint64_t perms,
                       uint64_t client_id,
                       uint64_t space_id,
                       uint64_t object_id);

/**
 * Make a universal identifier for a resource
 * 
 * @param type the resource type
 * @param space_id the resource space
 * @param object_id the resource ID, unique within the space
*/
uint64_t universal_res_id(gpi_cap_t type, uint64_t space_id, uint64_t object_id);

/**
 * @brief formats badge details into a string for printing
 *
 * @param dest string buffer
 * @param badge badge to print
 */
void badge_sprint(char *dest, seL4_Word badge);