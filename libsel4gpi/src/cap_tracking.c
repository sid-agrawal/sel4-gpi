/**
 * @file cap_tracking.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the methods to manipulate the cap's metadata object
 * @version 0.1
 * @date 2023-11-13
 *
 * @copyright Copyright (c) 2023
 *
 */

#include <sel4utils/process.h>
#include <sel4utils/vspace.h>

#include <sel4gpi/pd_component.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_server.h>


/*
    Add cap to the cap tracking object.
*/
int gpi_add_cap_data(osmosis_cap_t *new_cap_data)
{
    gpi_server_context_t *env = get_gpi_server();
    for (int i = 0; i < MAX_OSM_CAPS; i++) {

        /* Check that slot is free */
        if (env->osm_caps[i].slot == 0) {
            /* If the slot is free then the type should be cleared too*/
            assert (new_cap_data->type == GPICAP_TYPE_MAX);

            /* Initilize the slot field*/
            env->osm_caps[i].slot = new_cap_data->slot;
            env->osm_caps[i].type = new_cap_data->type;
            env->osm_caps[i].isUntyped = new_cap_data->isUntyped;
            env->osm_caps[i].paddr = new_cap_data->paddr;
            return 0;
        }

    }
    ZF_LOGE("Could not an empty entry in the cap tracking object");
    return 1;
}

/*
    Remove cap from the cap tracking object.
*/
int gpi_remove_cap_data(seL4_CPtr cap_to_remove)
{

    gpi_server_context_t *env = get_gpi_server();

    for (int i = 0; i < MAX_OSM_CAPS; i++)
    {
        if (env->osm_caps[i].slot == cap_to_remove)
        {
            /* Check that a type is set */
            assert(env->osm_caps[i].type != GPICAP_TYPE_MAX);

            /* Initilize the slot field*/
            env->osm_caps[i].slot = 0;

            /* Clear the actual data */
            env->osm_caps[i].type = GPICAP_TYPE_MAX;
            env->osm_caps[i].isUntyped = false;
            env->osm_caps[i].paddr = 0;

            /* (TODO) Find all caps, where this cap is a
                parent and delete those too.
            */
            for (int j = 0; j < MAX_OSM_CAPS; j++) {

                if (env->osm_caps[i].minted_from ==
                    cap_to_remove)
                {

                    /* Check that a type is set */
                    assert(env->osm_caps[i].type != GPICAP_TYPE_MAX);

                    /* Initilize the slot field*/
                    env->osm_caps[i].slot = 0;

                    /* Clear the actual data */
                    env->osm_caps[i].type = GPICAP_TYPE_MAX;
                    env->osm_caps[i].isUntyped = false;
                    env->osm_caps[i].paddr = 0;
                }
            }
            return 0;
        }
    }
    ZF_LOGE("Could not find cap in cap tracking object");
    return 1;
}

/*
    Retrive cap's data from the cap tracking object.
*/
int gpi_retrieve_cap_data(seL4_CPtr cap_to_find,
                     osmosis_cap_t *return_data)
{
    gpi_server_context_t *env = get_gpi_server();

    for (int i = 0; i < MAX_OSM_CAPS; i++)
    {
        if (env->osm_caps[i].slot == cap_to_find) {
            /* Check that a type is set */
            assert(env->osm_caps[i].type != GPICAP_TYPE_MAX);

            /* Initilize the slot field*/
            return_data->slot = env->osm_caps[i].slot;

            /* Retrive the actual data */
            return_data->type = env->osm_caps[i].type;
            return_data->isUntyped = env->osm_caps[i].isUntyped;
            return_data->paddr = env->osm_caps[i].paddr;
            return 0;
        }
    }
    ZF_LOGE("Could not find cap in cap tracking object for cap %lu", cap_to_find);
    return 1;
}
