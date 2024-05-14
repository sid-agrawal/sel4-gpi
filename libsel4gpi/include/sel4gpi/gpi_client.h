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

typedef enum _gpi_share_degree
{
   GPI_SHARED = 1,
   GPI_COPY,
   GPI_DISJOINT
} gpi_share_degree_t;

// Configuration types for creating new PDs: for a given resource, defines the level of sharing between a given source PD and the new PD

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
   char *image_name; // only used if code_shared == GPI_DISJOINT
   /* special ADS regions */
   gpi_share_degree_t code_shared;
   gpi_share_degree_t stack_shared;
   // gpi_share_degree_t heap_shared;
   uint32_t stack_pages;
   /* list of vaddrs to VMRs that should be shared */
   int n_shared;
   vmr_config_t shared_vmrs[MAX_SHARED_VMRS];
} ads_resource_config_t;

// For creating new PDs: defines the level of sharing between a given source PD and the new PD
typedef struct _pd_resource_config
{
   ads_resource_config_t ads_cfg;
   // TODO: cpu config
} pd_resource_config_t;

int sel4gpi_image_name_to_id(const char *image_name);
