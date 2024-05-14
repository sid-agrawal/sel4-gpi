#pragma once
#include <sel4/sel4.h>
#include <stdint.h>
#include <utils/page.h>
#include <vka/vka.h>
#include <sel4gpi/badge_usage.h>

/* (XXX) Arya: temp definition of supported images */
#define PD_N_IMAGES 6
static const char *pd_images[PD_N_IMAGES] = {"hello", "hello_kvstore", "ramdisk_server", "fs_server", "kvstore_server", "hello_benchmark"};

#define PD_MAX_ARGC 4
#define MAX_SHARED_VMRS 100

typedef struct _resspc_client_context
{
   cspacepath_t badged_server_ep_cspath;
   uint64_t id;
} resspc_client_context_t;

typedef struct _cpu_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
} cpu_client_context_t;

typedef struct _ads_client_context
{
   cspacepath_t badged_server_ep_cspath;
   uint64_t id;
   // cspacepath_t public_server_ep_cspath;
} ads_client_context_t;

typedef struct _ads_vmr_context
{
   cspacepath_t badged_server_ep_cspath;
} ads_vmr_context_t;

typedef struct _pd_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
} pd_client_context_t;

typedef struct _mo_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
   uint64_t id; // Needed only for RR dump
} mo_client_context_t;

// Configuration types for creating new PDs: for a given resource, defines the level of sharing between a given source PD and the new PD

// describes sharing between 2 PDs
typedef enum _gpi_share_degree
{
   GPI_SHARED = 1, // this resource is directly shared with the other PD, e.g. virt pages that map to the same phys page
   GPI_COPY,       // this resource is copied into the other PD, e.g. virt pages with separate phys pages with contents copied
   GPI_DISJOINT,   // this resource exists in the other PD, but has no relation with the source PD
   GPI_OMIT        // this resource will not exist in the other PD
} gpi_share_degree_t;

// configuration of a particular VMR, do not use for the stack and ELF regions
typedef struct _vmr_config
{
   gpi_share_degree_t share_mode;
   void *start;           // vaddr to start of the VMR
   uint64_t region_pages; // number of pages in this VMR
} vmr_config_t;

// Configuration of an entire ADS
typedef struct _ads_resource_config
{
   ads_client_context_t *src_ads; // the source ADS to generate the new ADS, if NULL, then this config describes the current ADS
   char *image_name; // only used if code_shared == GPI_DISJOINT
   /* special ADS regions */
   gpi_share_degree_t code_shared;
   gpi_share_degree_t stack_shared;
   gpi_share_degree_t heap_shared;
   gpi_share_degree_t ipc_buf_shared;
   int stack_pages;
   int heap_pages;
   /* list of vaddrs to VMRs that should be shared */
   int n_vmr_shared;
   vmr_config_t shared_vmrs[MAX_SHARED_VMRS];
} ads_resource_config_t;

// For creating new PDs: defines the level of sharing between a given source PD and the new PD
typedef struct _pd_resource_config
{
   ads_resource_config_t ads_cfg;
   // add configs for other resources here as needed
} pd_resource_config_t;

int sel4gpi_image_name_to_id(const char *image_name);
