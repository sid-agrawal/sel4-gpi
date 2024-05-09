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

typedef enum GPICAP_TYPE
{
    // Core cap types
    GPICAP_TYPE_NONE = 0,
    GPICAP_TYPE_ADS,
    GPICAP_TYPE_VMR,
    GPICAP_TYPE_MO,
    GPICAP_TYPE_PMR,
    GPICAP_TYPE_CPU,
    GPICAP_TYPE_PCPU,
    GPICAP_TYPE_PD,
    GPICAP_TYPE_RS, // resource space
    GPICAP_TYPE_seL4,

    // Non-core cap types
    GPICAP_TYPE_BLOCK,
    GPICAP_TYPE_FILE,

    GPICAP_TYPE_MAX,
} gpi_cap_t;

// (XXX) Arya: Should be able to make this 0 once we prefix badges with server ID
#define BADGE_OBJ_ID_NULL 0xfffff

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

// Bits: 55:40 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
uint64_t get_ns_id_from_badge(seL4_Word badge);

// Bits: 55:40 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
uint64_t set_ns_id_to_badge(seL4_Word badge, uint64_t ns_id);

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
uint64_t get_client_id_from_badge(seL4_Word badge);

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
uint64_t set_client_id_to_badge(seL4_Word badge, uint64_t client_id);

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
uint64_t get_object_id_from_badge(seL4_Word badge);

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
uint64_t set_object_id_to_badge(seL4_Word badge, uint64_t object_id);

// Bits: 19:16 are for the server id. Total of 4 bits, so 16 resource servers.
uint64_t set_server_id_to_badge(seL4_Word badge, uint64_t server_id);

// Bits: 19:16 are for the server id. Total of 4 bits, so 16 resource servers.
uint64_t get_server_id_from_badge(seL4_Word badge);

// Sets local object ID, unique to a given server, but not unique globally
// 2^16 objects per server.
uint64_t set_local_object_id_to_badge(seL4_Word badge, uint64_t object_id);

// Gets local object ID, unique to a given server, but not unique globally
// 2^16 objects per server
uint64_t get_local_object_id_from_badge(seL4_Word badge);

// Combine server id and local object id to get global object id
uint64_t get_global_object_id_from_local(uint64_t server_id, uint64_t object_id);

uint64_t gpi_new_badge(gpi_cap_t cap_type,
                       uint64_t perms,
                       uint64_t client_id,
                       uint64_t ns_id,
                       uint64_t object_id);

// Badges an endpoint for a resource server, with server id
uint64_t gpi_new_badge_server(gpi_cap_t cap_type,
                              uint64_t perms,
                              uint64_t client_id,
                              uint64_t server_id,
                              uint64_t ns_id,
                              uint64_t object_id);

void badge_print(seL4_Word badge);
void gpi_panic(char *reason, uint64_t code);
char *cap_type_to_str(gpi_cap_t cap_type);