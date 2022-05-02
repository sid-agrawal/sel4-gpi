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


typedef enum GPICAP_TYPE {
    GPICAP_TYPE_NONE = 0,
    GPICAP_TYPE_ADS,
    GPICAP_TYPE_CPU,
    GPICAP_TYPE_MAX,
}gpi_cap_t;

/*
 How we are using the badge.
 There are a total of 64 bits.
63:56  8 bits for the type of cap.
55:40 16 bits for permissions, as a bit mask.
39:20 20 bits for client ID
19:0  20 bits for object ID 
*/


// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
uint64_t get_cap_type_from_badge(seL4_Word badge);

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
uint64_t set_cap_type_to_badge(seL4_Word badge, uint64_t type);

// Bits: 55:40 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
uint64_t get_perms_from_badge(seL4_Word badge);

// Bits: 55:40 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
uint64_t set_perms_to_badge(seL4_Word badge, uint64_t perms);

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
uint64_t get_client_id_from_badge(seL4_Word badge);

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
uint64_t set_client_id_to_badge(seL4_Word badge, uint64_t client_id);

// Bits: 19:0 are for the object id. Total of 32 bits, so 2^20 objects.
uint64_t get_object_id_from_badge(seL4_Word badge);

// Bits: 31:0 are for the object id. Total of 32 bits, so 2^32 objects.
uint64_t set_object_id_to_badge(seL4_Word badge, uint64_t object_id);

uint64_t gpi_new_badge(gpi_cap_t cap_type,
                            uint64_t perms,
                            uint64_t client_id,
                            uint64_t object_id);

void badge_print(seL4_Word badge);
void gpi_panic(char *reason);