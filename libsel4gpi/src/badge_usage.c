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
63:56 8 bits for the type of cap.
55:48 8 bits for permissions, as a bit mask.
47:40 8 bits for the space ID
39:20 20 bits for client ID
19:0  20 bits for object ID
*/

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
gpi_cap_t get_cap_type_from_badge(gpi_badge_t badge)
{
    return (badge >> 56) & 0xFF;
}

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
gpi_badge_t set_cap_type_to_badge(gpi_badge_t badge, gpi_cap_t type)
{
    assert(type <= 0xFF);
    uint64_t shifted_type = type;
    shifted_type = shifted_type << 56;
    return (badge & 0x00FFFFFFFFFFFFFF) | shifted_type;
}

// Bits: 55:48 are for the permisions. Total of 8 bits, as a bit-mask so 8 permissions.
gpi_perms_t get_perms_from_badge(gpi_badge_t badge)
{
    return (badge >> 48) & 0xFF;
}

// Bits: 55:48 are for the permisions. Total of 8 bits, as a bit-mask so 8 permissions.
gpi_badge_t set_perms_to_badge(gpi_badge_t badge, gpi_perms_t perms)
{
    assert(perms <= 0xFF);
    uint64_t shifted_perms = perms;
    shifted_perms = shifted_perms << 56;
    return (badge & 0xFF00FFFFFFFFFFFF) | shifted_perms;
}

// Bits: 47:40 are for the resource space ID. Total of 8 bits, so 255 resource spaces
gpi_space_id_t get_space_id_from_badge(gpi_badge_t badge)
{
    return (badge >> 40) & 0xFF;
}

// Bits: 47:40 are for the resource space ID. Total of 8 bits, so 255 resource spaces
gpi_badge_t set_space_id_to_badge(gpi_badge_t badge, gpi_space_id_t space_id)
{
    assert(space_id <= 0xFF);
    uint64_t shifted_space_id = space_id;
    shifted_space_id = shifted_space_id << 40;
    return (badge & 0xFFFF00FFFFFFFFFF) | shifted_space_id;
}

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
gpi_obj_id_t get_client_id_from_badge(gpi_badge_t badge)
{
    return (badge >> 20) & 0xFFFFF;
}

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
gpi_badge_t set_client_id_to_badge(gpi_badge_t badge, gpi_obj_id_t client_id)
{
    assert(client_id <= 0xFFFFF);
    uint64_t shifted_client_id = client_id;
    shifted_client_id = shifted_client_id << 20;
    return (badge & 0xFFFFFF00000FFFFF) | shifted_client_id;
}

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
gpi_obj_id_t get_object_id_from_badge(gpi_badge_t badge)
{
    return (badge & 0xFFFFF);
}

// Bits: 19:0 are for the object id. Total of 20 bits, so 2^20 objects.
gpi_badge_t set_object_id_to_badge(gpi_badge_t badge, gpi_obj_id_t object_id)
{
    assert(object_id <= 0xFFFFF);
    return (badge & 0xFFFFFFFFFFF00000) | object_id;
}

gpi_badge_t gpi_new_badge(gpi_cap_t cap_type,
                          gpi_perms_t perms,
                          gpi_obj_id_t client_id,
                          gpi_space_id_t space_id,
                          gpi_obj_id_t object_id)
{
    gpi_badge_t badge_value = 0;
    badge_value = set_cap_type_to_badge(badge_value, cap_type);
    badge_value = set_perms_to_badge(badge_value, perms);
    badge_value = set_object_id_to_badge(badge_value, object_id);
    badge_value = set_client_id_to_badge(badge_value, client_id);
    badge_value = set_space_id_to_badge(badge_value, space_id);

    assert(badge_value != NOTIF_BADGE);

    return badge_value;
}

gpi_badge_t compact_res_id(gpi_cap_t type, gpi_space_id_t space_id, gpi_obj_id_t object_id)
{
    return gpi_new_badge(type, 0, 0, space_id, object_id);
}

void badge_sprint(char *dest, gpi_badge_t badge)
{
    sprintf(dest, "BG: %lx\tCapType: %s\tPerms: %u\tSpaceID: %u\tCID: %u\tOID: %u",
            badge,
            cap_type_to_str(get_cap_type_from_badge(badge)),
            get_perms_from_badge(badge),
            get_space_id_from_badge(badge),
            get_client_id_from_badge(badge),
            get_object_id_from_badge(badge));
}
