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

#define BADGE_MAX_CAP_TYPE 0xFF
#define BADGE_MAX_SPACE_ID 0xFF
#define BADGE_MAX_OBJ_ID 0xFFFFF
#define BADGE_MAX_CLIENT_ID 0xFFFFF
#define BADGE_MAX 0xFFFFFFFFFFFFFFFF

#define NOTIF_BADGE BADGE_MAX // Badge value reserved for RT->PD notifications
#define BADGE_OBJ_ID_NULL BADGE_MAX_OBJ_ID
#define BADGE_SPACE_ID_NULL BADGE_MAX_SPACE_ID

// requires that SERVER_ID and DEBUG_ID are defined
#define BADGE_PRINT(badge)                                                                 \
    do                                                                                     \
    {                                                                                      \
        OSDB_PRINTF_2("BG: %lx\t", (badge));                                               \
        OSDB_PRINTF_2("CapType: %s\t", cap_type_to_str(get_cap_type_from_badge((badge)))); \
        OSDB_PRINTF_2("Perms: %u\t", get_perms_from_badge((badge)));                       \
        OSDB_PRINTF_2("SpaceID: %u\t", get_space_id_from_badge((badge)));                  \
        OSDB_PRINTF_2("CID: %u\t", get_client_id_from_badge((badge)));                     \
        OSDB_PRINTF_2("OID: %u\n", get_object_id_from_badge((badge)));                     \
    } while (0)

/*
How we are using the badge.
There are a total of 64 bits.
63:56 8 bits for the type of cap.
55:48 8 bits for permissions, as a bit mask.
47:40 8 bits for the space ID
39:20 20 bits for client ID
19:0  20 bits for object ID
*/

typedef seL4_Word gpi_badge_t;
typedef uint16_t gpi_perms_t;

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
gpi_cap_t get_cap_type_from_badge(gpi_badge_t badge);

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
gpi_badge_t set_cap_type_to_badge(gpi_badge_t badge, gpi_cap_t type);

// Bits: 55:40 are for the permisions. Total of 8 bits, as a bit-mask so 8 permissions.
gpi_perms_t get_perms_from_badge(gpi_badge_t badge);

// Bits: 55:40 are for the permisions. Total of 8 bits, as a bit-mask so 8 permissions.
gpi_badge_t set_perms_to_badge(gpi_badge_t badge, gpi_perms_t perms);

// Bits: 47:40 are for the resource space ID. Total of 8 bits, so 255 resource spaces
uint32_t get_space_id_from_badge(gpi_badge_t badge);

// Bits: 47:40 are for the resource space ID. Total of 8 bits, so 255 resource spaces
gpi_badge_t set_space_id_to_badge(gpi_badge_t badge, gpi_space_id_t space_id);

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
gpi_obj_id_t get_client_id_from_badge(gpi_badge_t badge);

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
gpi_badge_t set_client_id_to_badge(gpi_badge_t badge, gpi_obj_id_t client_id);

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
gpi_obj_id_t get_object_id_from_badge(gpi_badge_t badge);

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
gpi_badge_t set_object_id_to_badge(gpi_badge_t badge, gpi_obj_id_t object_id);

gpi_badge_t gpi_new_badge(gpi_cap_t cap_type,
                          gpi_perms_t perms,
                          gpi_obj_id_t client_id,
                          gpi_space_id_t space_id,
                          gpi_obj_id_t object_id);

/**
 * Make a compact, unique identifier for a resource
 * This uses the same format as resource badges
 *
 * @param type the resource type
 * @param space_id the resource space
 * @param object_id the resource ID, unique within the space
 */
gpi_badge_t compact_res_id(gpi_cap_t type, gpi_space_id_t space_id, gpi_obj_id_t object_id);

/**
 * @brief formats badge details into a string for printing
 *
 * @param dest string buffer
 * @param badge badge to print
 */
void badge_sprint(char *dest, gpi_badge_t badge);