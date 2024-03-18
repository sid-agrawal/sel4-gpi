#include <sel4runtime.h>
#include <sel4gpi/pd_clientapi.h>

#include <sel4gpi/pd_utils.h>

seL4_CPtr sel4gpi_get_pd_cap(void)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->pd_cap;
    return slot;
}

seL4_CPtr sel4gpi_get_ads_cap(void)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->ads_cap;
    return slot;
}

seL4_CPtr sel4gpi_get_rde(int type)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->rde[type].slot_in_PD;
    return slot;
}

uint64_t sel4gpi_get_binded_ads_id(void)
{
    return ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->binded_ads_ns_id;
}

seL4_CPtr sel4gpi_get_rde_by_ns_id(uint32_t ns_id, gpi_cap_t type)
{
    assert(type != GPICAP_TYPE_NONE && type != GPICAP_TYPE_MAX);
    osmosis_rde_t *rde = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->rde;
    if (ns_id == 0)
    {
        return sel4gpi_get_rde(type);
    }
    else
    {
        int start = GPICAP_TYPE_MAX + (MAX_NS_PER_RDE * (type - 1));
        seL4_CPtr found_rde = seL4_CapNull;

        for (int i = start; i < start + MAX_NS_PER_RDE; i++)
        {
            if (rde[i].ns_id == ns_id)
            {
                found_rde = rde[i].slot_in_PD;
                break;
            }
        }

        return found_rde;
    }
}
