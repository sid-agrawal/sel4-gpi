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

#include <assert.h>
#include <stdio.h>

#include <sel4gpi/debug.h>
#include <sel4gpi/resource_types.h>
#include <sel4gpi/badge_usage.h>

/*
 How we are using the badge.
 There are a total of 64 bits.
63:56  8 bits for the type of cap.
55:40 16 bits for permissions, as a bit mask.
39:20 20 bits for client ID
19:0  20 bits for object ID
*/

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
uint64_t get_cap_type_from_badge(seL4_Word badge)
{
    return (badge >> 56) & 0xFF;
}

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
uint64_t set_cap_type_to_badge(seL4_Word badge, uint64_t type)
{
    assert(type <= 0xFF);
    return (badge & 0x00FFFFFFFFFFFFFF) | (type << 56);
}

// Bits: 55:48 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
uint64_t get_perms_from_badge(seL4_Word badge)
{
    return (badge >> 48) & 0xFF;
}

// Bits: 55:48 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
uint64_t set_perms_to_badge(seL4_Word badge, uint64_t perms)
{
    assert(perms <= 0xFF);
    return (badge & 0xFF00FFFFFFFFFFFF) | (perms << 48);
}

// Bits: 47:40 are for the resource space ID. Total of 8 bits, so 255 resource spaces
uint64_t get_space_id_from_badge(seL4_Word badge)
{
    return (badge >> 40) & 0xFF;
}

// Bits: 47:40 are for the resource space ID. Total of 8 bits, so 255 resource spaces
uint64_t set_space_id_to_badge(seL4_Word badge, uint64_t space_id)
{
    assert(space_id <= 0xFF);
    return (badge & 0xFFFF00FFFFFFFFFF) | (space_id << 40);
}

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
uint64_t get_client_id_from_badge(seL4_Word badge)
{
    return (badge >> 20) & 0xFFFFF;
}

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
uint64_t set_client_id_to_badge(seL4_Word badge, uint64_t client_id)
{
    assert(client_id <= 0xFFFFF);
    return (badge & 0xFFFFFF00000FFFFF) | (client_id << 20);
}

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
uint64_t get_object_id_from_badge(seL4_Word badge)
{
    return (badge & 0xFFFFF);
}

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
uint64_t set_object_id_to_badge(seL4_Word badge, uint64_t object_id)
{
    assert(object_id <= 0xFFFFF);
    return (badge & 0xFFFFFFFFFFF00000) | object_id;
}

uint64_t gpi_new_badge(gpi_cap_t cap_type,
                       uint64_t perms,
                       uint64_t client_id,
                       uint64_t space_id,
                       uint64_t object_id)
{
    uint64_t badge_value = 0;
    badge_value = set_cap_type_to_badge(badge_value, cap_type);
    badge_value = set_perms_to_badge(badge_value, perms);
    badge_value = set_object_id_to_badge(badge_value, object_id);
    badge_value = set_client_id_to_badge(badge_value, client_id);
    badge_value = set_space_id_to_badge(badge_value, space_id);
    return badge_value;
}

uint64_t universal_res_id(gpi_cap_t type, uint64_t space_id, uint64_t object_id)
{
    return gpi_new_badge(type, 0, 0, space_id, object_id);
}

void badge_sprint(char *dest, seL4_Word badge)
{
    sprintf(dest, "BG: %lx\tCapType: %s\tPerms: %lu\tSpaceID: %lu\tCID: %lu\tOID: %lu",
            badge,
            cap_type_to_str(get_cap_type_from_badge(badge)),
            get_perms_from_badge(badge),
            get_space_id_from_badge(badge),
            get_client_id_from_badge(badge),
            get_object_id_from_badge(badge));
}
