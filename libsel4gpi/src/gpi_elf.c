/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <autoconf.h>
#include <sel4utils/gen_config.h>

#include <string.h>
#include <sel4/sel4.h>
#include <elf/elf.h>
#include <vka/capops.h>
#include <sel4utils/thread.h>
#include <sel4utils/util.h>
#include <sel4utils/mapping.h>
#include <sel4gpi/gpi_elf.h>
#include <sel4gpi/mo_component.h>

#define GPI_ELF_DEBUG 0

/*
 * Convert ELF permissions into seL4 permissions.
 *
 * @param permissions elf permissions
 * @return seL4 permissions
 */
static inline seL4_CapRights_t rights_from_elf(unsigned long permissions)
{
    bool canRead = permissions & PF_R || permissions & PF_X;
    bool canWrite = permissions & PF_W;

    return seL4_CapRights_new(false, false, canRead, canWrite);
}

/**
 * Copy from source to dest
 * If the length to copy is greater than the remaining file size, copies zeroes for the remaining size
 *
 * @param dest the destination vaddr
 * @param src the source file
 * @param src_file_size total size of the source file
 * @param src_offset offset into the source file to start at
 * @param len length to write at the destination vaddr
 */
static void copy_region(void *dest, char *src, size_t src_file_size, size_t src_offset, size_t len)
{
    size_t len_from_src = src_file_size - src_offset;

    // Write as much of source as possible
    if (len_from_src > 0)
    {
        memcpy(dest, src + src_offset, len_from_src);
        len -= len_from_src;
        dest += len_from_src;
    }

    // Write zeroes for the remaining
    // (XXX) Arya: should this always be zero?
    if (len > 0)
    {
        memset(dest, 0, len);
    }
}

/**
 * Load an array of regions into an ads.
 *
 * The region array passed in won't be mutated by this function or functions it calls.
 * State in the adses and vkas will be mutated to track resources used.
 * If this function fails, any allocated and mapped frames will not be freed.
 *
 * @param loadee_ads target ads to map frames into.
 * @param loader_ads ads of the caller.  Frames are temporarily mapped into this to init with
 *                      elf data from elf file.
 * @param loadee_vka target vka
 * @param loader_vka caller vka
 * @param elf_file pointer to elf object
 * @param num_regions total number of segments/regions to load.
 * @param regions region array containing segment info.
 *
 * @return 0 on success.
 */
static int load_segments(ads_t *loadee_ads, ads_t *loader_ads,
                         vka_t *loadee_vka, vka_t *loader_vka, const elf_t *elf_file,
                         int num_regions, sel4gpi_elf_region_t regions[num_regions])
{
    int error = 0;

    int64_t overflow_bytes = 0;
    size_t overflow_src_offset = 0;

    for (int i = 0; i < num_regions; i++)
    {
        sel4gpi_elf_region_t region = regions[i];
        size_t segment_size = region.size;
        int segment_index = region.segment_index;

        if (region.src == NULL)
        {
            return 1;
        }

        if (region.src_size > segment_size)
        {
            ZF_LOGE("Error, file_size %zu > segment_size %zu", region.src_size, segment_size);
            return seL4_InvalidArgument;
        }

        /**
         * (XXX) Arya:
         * Modified from sel4utils load_segment
         * This used to work one page at a time, I have modified the logic
         * to make it easier to use with the ADS component, but I am not certain if it is still correct
         **/
        unsigned int pos = 0;                         // Position in the region
        uintptr_t dst = (uintptr_t)region.elf_vstart; // Destination addr in the loadee
        void *loader_vaddr = 0;
        void *loadee_vaddr = (void *)((seL4_Word)ROUND_DOWN(dst, PAGE_SIZE_4K));
        void *vaddr_to_write = NULL;
        void *loader_ptr = NULL;
        size_t size_to_write = 0;

        /* Reserve the region's memory */
        mo_t *mo;
        error = mo_component_allocate_rt(region.reservation_pages, &mo);
        if (error)
        {
            ZF_LOGE("Error, failed to allocate MO for elf segment");
            return error;
        }

        /* Map region's memory to loadee */
        error = ads_attach_to_res(loadee_ads, loadee_vka, region.reservation, 0, mo);
        if (error)
        {
            ZF_LOGE("Error, failed to attach MO to loadee for elf segment");
            return error;
        }

        /* Map region's memory to loader for copying */
        error = ads_attach(loader_ads, loader_vka, NULL, mo, true,
                           seL4_ReadWrite, &loader_vaddr, SEL4UTILS_RES_TYPE_GENERIC);
        if (error)
        {
            ZF_LOGE("Error, failed to attach MO to loader for elf segment");
            return error;
        }
        loader_ptr = loader_vaddr;

        /* Check if the previous region has some overflow */
        if (overflow_bytes > 0)
        {
            vaddr_to_write = loader_vaddr;
            size_to_write = overflow_bytes;

#if GPI_ELF_DEBUG
            printf("gpi_elf: writing overflow from region %d to [%p,%p]  -> [%p,%p]\n",
                   i - 1,
                   vaddr_to_write,
                   vaddr_to_write + size_to_write,
                   loadee_vaddr + (vaddr_to_write - loader_vaddr),
                   loadee_vaddr + (vaddr_to_write - loader_vaddr) + size_to_write);
#endif

            copy_region(vaddr_to_write, regions[i - 1].src, regions[i - 1].src_size, overflow_src_offset, size_to_write);
            loader_ptr += size_to_write;
        }

        /* Write this region's data (as much as possible) */
        vaddr_to_write = loader_vaddr;
        size_to_write = region.size;
        int64_t underflow = (region.reservation_vstart - region.elf_vstart);
        size_t src_offset = 0;

        if (underflow > 0)
        {
            // Some of this region was already written to the previous reservation
            size_to_write -= underflow;
            src_offset = underflow;
        }
        else if (underflow < 0)
        {
            // Some padding at the beginning of the reservation
            vaddr_to_write += -underflow;
        }

        overflow_bytes = (vaddr_to_write + size_to_write) - (loader_vaddr + region.reservation_size);
        if (overflow_bytes > 0)
        {
            size_to_write -= overflow_bytes;
            overflow_src_offset = size_to_write; // Next iteration will handle this overflow
        }

#if GPI_ELF_DEBUG
        printf("gpi_elf: writing region %d to [%p,%p] -> [%p,%p]\n",
               i,
               vaddr_to_write,
               vaddr_to_write + size_to_write,
               loadee_vaddr + (vaddr_to_write - loader_vaddr),
               loadee_vaddr + (vaddr_to_write - loader_vaddr) + size_to_write);
#endif

        if (vaddr_to_write < loader_ptr)
        {
            // Overflow from last region is beyond the start of this region
            ZF_LOGE("Invalid regions: bad elf file.");
            return seL4_InvalidArgument;
        }

        copy_region(vaddr_to_write, region.src, region.src_size, src_offset, size_to_write);
        loader_ptr = vaddr_to_write + size_to_write;

        /* Check if the next region has some underflow */
        if (i < num_regions - 1)
        {
            int64_t next_underflow = (regions[i + 1].reservation_vstart - regions[i + 1].elf_vstart);

            if (next_underflow > 0)
            {
                vaddr_to_write = loader_vaddr + region.reservation_size - next_underflow;
                size_to_write = next_underflow;

                if (vaddr_to_write < loader_ptr)
                {
                    // Underflow writes over the end of this region
                    ZF_LOGE("Invalid regions: bad elf file.");
                    return seL4_InvalidArgument;
                }

#if GPI_ELF_DEBUG
                printf("gpi_elf: underflow from region %d to [%p,%p]  -> [%p,%p]\n",
                       i + 1,
                       vaddr_to_write,
                       vaddr_to_write + size_to_write,
                       loadee_vaddr + (vaddr_to_write - loader_vaddr),
                       loadee_vaddr + (vaddr_to_write - loader_vaddr) + size_to_write);
#endif

                copy_region(vaddr_to_write, regions[i + 1].src, regions[i + 1].src_size, 0, size_to_write);
            }
        }

#ifdef CONFIG_ARCH_ARM
        /* Flush the caches */
        attach_node_t *loader_attach = ads_get_res_by_vaddr(loader_ads, loader_vaddr);
        attach_node_t *loadee_attach = ads_get_res_by_vaddr(loadee_ads, loadee_vaddr);

        for (int i = 0; i < region.reservation_pages; i++)
        {
            seL4_ARM_Page_Unify_Instruction(loader_attach->frame_caps[i], 0, PAGE_SIZE_4K);
            seL4_ARM_Page_Unify_Instruction(loadee_attach->frame_caps[i], 0, PAGE_SIZE_4K);
        }
#elif CONFIG_ARCH_RISCV
        /* Ensure that the writes to memory that may be executed become visible */
        asm volatile("fence.i" ::: "memory");
#endif

        /* Remove memory from loader */
        error = ads_rm(loader_ads, loader_vka, loader_vaddr);
        if (error)
        {
            ZF_LOGE("Error, failed to remove MO from loader for elf segment");
            return error;
        }
    }

    return 0;
}

static bool is_executable_segment(const elf_t *elf_file, int index)
{
    return elf_getProgramHeaderFlags(elf_file, index) & PF_X;
}

static bool is_loadable_section(const elf_t *elf_file, int index)
{
    return elf_getProgramHeaderType(elf_file, index) == PT_LOAD;
}

static int count_loadable_regions(const elf_t *elf_file)
{
    int num_headers = elf_getNumProgramHeaders(elf_file);
    int loadable_headers = 0;

    for (int i = 0; i < num_headers; i++)
    {
        /* Skip non-loadable segments (such as debugging data). */
        if (is_loadable_section(elf_file, i))
        {
            loadable_headers++;
        }
    }
    return loadable_headers;
}

int sel4gpi_elf_num_regions(const elf_t *elf_file)
{
    return count_loadable_regions(elf_file);
}

/**
 * Create reservations for regions in a target ads.
 *
 * The region position and size fields should have already been calculated by prepare_reservations.
 *
 * @param loadee the ads to load into.
 * @param total_regions the size of the regions array
 * @param regions the array of regions.
 * @param anywhere some legacy parameter that throws away the virtual address.  It is supposedly to support
                   loading a segment for inspection rather than execution.
 *
 * @return 0 on success.
 */
static int create_reservations(ads_t *loadee, size_t total_regions, sel4gpi_elf_region_t regions[total_regions],
                               int anywhere)
{
    for (int i = 0; i < total_regions; i++)
    {
        if (regions[i].reservation_size == 0)
        {
            ZF_LOGD("Empty reservation detected. This should indicate that this segments"
                    "data is entirely stored in other section reservations.");
            continue;
        }

        assert(SIZE_BITS_TO_BYTES(MO_PAGE_BITS) == PAGE_SIZE_4K);

        attach_node_t *attach_node;
        void *requested_addr = anywhere ? NULL : regions[i].reservation_vstart;
        sel4utils_reservation_type_t type = regions[i].executable ? SEL4UTILS_RES_TYPE_CODE : SEL4UTILS_RES_TYPE_DATA;
        // (XXX) Arya: get the reservation type
        // (XXX) Arya: add the cache/rights rags
        int error = ads_reserve(loadee, regions[i].reservation_vstart, regions[i].reservation_pages, MO_PAGE_BITS,
                                type, regions[i].cacheable, regions[i].rights, &regions[i].reservation);

        if (error)
        {
            ZF_LOGE("Failed to make reservation: %p, %zd", regions[i].reservation_vstart, regions[i].reservation_size);
            return -1;
        }

        if (anywhere)
        {
            regions[i].reservation_vstart = regions[i].reservation->vaddr;
        }
    }
    return 0;
}

/**
 * Function for deciding whether a frame needs to be moved to a different reservation.
 *
 * Mapping permissions are set for a whole reservation.  With adjacent segments of different
 * permissions, we need to give the shared frame mapping to the reservation with the more permissive
 * permissions.  Currently this assumes that every region will have read permissions, and the frame
 * only needs to be moved if the lower region doesn't have write permissions and the upper one does.
 *
 * @param a CapRights for reservation a.
 * @param b CapRights for reservation b.
 * @param result whether the frame should be moved.
 *
 * @return 0 on success.
 */
static int cap_writes_check_move_frame(seL4_CapRights_t a, seL4_CapRights_t b, bool *result)
{
    if (!seL4_CapRights_get_capAllowRead(a) || !seL4_CapRights_get_capAllowRead(b))
    {
        ZF_LOGE("Regions do not have read rights.");
        return -1;
    }
    if (!seL4_CapRights_get_capAllowWrite(a) && seL4_CapRights_get_capAllowWrite(b))
    {
        *result = true;
        return 0;
    }
    *result = false;
    return 0;
}

/**
 * Prepares a list of regions to have reservations reserved by an ads.
 *
 * Iterates through a region array in ascending order.
 * For each region it tries to place a reservation that contains the segment.
 * Reservations are rounded up to 4k alignments.  This means that the previous reservation
 * may overlap with the start of the current region.  When this occurs, the last frame of the
 * previous reservation may need to be moved to the current reservation.  This is decided based
 * on the reservation permissions by the cap_writes_check_move_frame function.
 * If the frame doesn't need to be moved, then this reservation starts from the first unreserved
 * frame of the segment.  When the segment is eventually loaded, the frames may need to be mapped
 * from from other segment's reservations.
 *
 * @param total_regions total number of regions in array.
 * @param regions array of regions sorted in ascending order.
 *
 * @return 0 on success.
 */
static int prepare_reservations(size_t total_regions, sel4gpi_elf_region_t regions[total_regions])
{
    uintptr_t prev_res_start = 0;
    size_t prev_res_size = 0;
    seL4_CapRights_t prev_rights = seL4_NoRights;
    for (int i = 0; i < total_regions; i++)
    {
        uintptr_t current_res_start = PAGE_ALIGN_4K((uintptr_t)regions[i].elf_vstart);
        uintptr_t current_res_top = ROUND_UP((uintptr_t)regions[i].elf_vstart + regions[i].size, PAGE_SIZE_4K);
        size_t current_res_size = current_res_top - current_res_start;
        assert(current_res_size % PAGE_SIZE_4K == 0);
        seL4_CapRights_t current_rights = regions[i].rights;

        if ((prev_res_start + prev_res_size) > current_res_start)
        {
            /* This segment shares a frame with the previous segment */
            bool should_move;
            int error = cap_writes_check_move_frame(prev_rights, current_rights, &should_move);
            if (error)
            {
                /* Comparator function failed. Return error. */
                return -1;
            }
            if (should_move)
            {
                /* Frame needs to be moved from the last reservation into this one */
                ZF_LOGF_IF(i == 0, "Should not need to adjust first element in list");
                ZF_LOGF_IF(regions[i - 1].reservation_size < PAGE_SIZE_4K, "Invalid previous region");
                regions[i - 1].reservation_size -= PAGE_SIZE_4K;
            }
            else
            {
                /* Frame stays in previous reservation and we update our reservation start address and size */
                current_res_start = ROUND_UP((prev_res_start + prev_res_size) + 1, PAGE_SIZE_4K);
                current_res_size = current_res_top - current_res_start;
                ZF_LOGF_IF(ROUND_UP(regions[i].size, PAGE_SIZE_4K) - current_res_size == PAGE_SIZE_4K,
                           "Regions shouldn't overlap by more than a single 4k frame");
            }
        }
        /* Record this reservation layout */
        regions[i].reservation_size = current_res_size;
        regions[i].reservation_pages = DIV_ROUND_UP(regions[i].reservation_size, PAGE_SIZE_4K);
        regions[i].reservation_vstart = (void *)current_res_start;
        prev_res_size = current_res_size;
        prev_res_start = current_res_start;
        prev_rights = current_rights;
    }
    return 0;
}

/**
 * Reads segment data out of elf file and creates region list.
 *
 * The region array must have the correct size as calculated by count_loadable_regions.
 *
 * @param elf_file pointer to start of elf_file
 * @param total_regions total number of loadable segments.
 * @param regions array of regions.
 *
 * @return 0 on success.
 */
static int read_regions(const elf_t *elf_file, size_t total_regions, sel4gpi_elf_region_t regions[total_regions])
{
    int num_headers = elf_getNumProgramHeaders(elf_file);
    int region_id = 0;
    for (int i = 0; i < num_headers; i++)
    {

        /* Skip non-loadable segments (such as debugging data). */
        if (is_loadable_section(elf_file, i))
        {
            sel4gpi_elf_region_t *region = &regions[region_id];
            /* Fetch information about this segment. */

            region->cacheable = 1;
            region->rights = rights_from_elf(elf_getProgramHeaderFlags(elf_file, i));
            region->executable = is_executable_segment(elf_file, i);
            // elf_getProgramHeaderMemorySize should just return `uintptr_t`
            region->elf_vstart = (void *)elf_getProgramHeaderVaddr(elf_file, i);
            region->size = elf_getProgramHeaderMemorySize(elf_file, i);
            region->segment_index = i;
            region->src = elf_getProgramSegment(elf_file, i);
            region->src_size = elf_getProgramHeaderFileSize(elf_file, i);
            region_id++;
        }
    }
    if (region_id != total_regions)
    {
        ZF_LOGE("Did not correctly read all regions.");
        return 1;
    }

    return 0;
}

/**
 * Compare function for ordering regions. Passed to sglib quick sort.
 *
 * Sort is based on base address.  This assumes segments do not overlap.
 * There is likely a chance that quick sort won't terminate if segments overlap.
 *
 * @param a region a
 * @param b region b
 *
 * @return 1 for a > b, -1 for b > a
 */
static int compare_regions(sel4gpi_elf_region_t a, sel4gpi_elf_region_t b)
{
    if (a.elf_vstart + a.size <= b.elf_vstart)
    {
        return -1;
    }
    else if (b.elf_vstart + b.size <= a.elf_vstart)
    {
        return 1;
    }
    else
    {
        ZF_LOGF("Bad elf file: segments overlap");
        return 0;
    }
}

/**
 * Parse an elf file and create reservations in a target ads for all loadable segments.
 *
 * Reads segment layout data out of elf file and stores in elf_region array.
 * Then sorts the array, then plans reservations based on segment layout.
 * Finally creates reservations in the ads.
 *
 * @param loadee ads to create reservations in.
 * @param elf_file pointer to elf file.
 * @param num_regions number of regions in array as calculated by count_loadable_regions.
 * @param regions region array.
 * @param mapanywhere throw away ads positioning if set to 1.
 *
 * @return 0 on success.
 */
static int elf_reserve_regions_in_ads(ads_t *loadee, const elf_t *elf_file,
                                      int num_regions, sel4gpi_elf_region_t regions[num_regions], int mapanywhere)
{
    int error = read_regions(elf_file, num_regions, regions);
    if (error)
    {
        ZF_LOGE("Failed to read regions");
        return error;
    }

    /* Sort region list */
    SGLIB_ARRAY_SINGLE_QUICK_SORT(sel4gpi_elf_region_t, regions, num_regions, compare_regions);

    error = prepare_reservations(num_regions, regions);
    if (error)
    {
        ZF_LOGE("Failed to prepare reservations");
        return error;
    }

    error = create_reservations(loadee, num_regions, regions, mapanywhere);
    if (error)
    {
        ZF_LOGE("Failed to create reservations");
        return error;
    }
    return 0;
}

static void *entry_point(const elf_t *elf_file)
{
    uint64_t entry_point = elf_getEntryPoint(elf_file);
    if ((uint32_t)(entry_point >> 32) != 0)
    {
        ZF_LOGE("ERROR: this code hasn't been tested for 64bit!");
        return NULL;
    }
    assert(entry_point != 0);
    return (void *)(seL4_Word)entry_point;
}

static void *sel4gpi_elf_load_record_regions(ads_t *loadee, ads_t *loader, vka_t *loadee_vka, vka_t *loader_vka,
                                             const elf_t *elf_file, sel4gpi_elf_region_t *regions, int mapanywhere)
{
    /* Calculate number of loadable regions.  Use stack array if one wasn't passed in */
    int num_regions = count_loadable_regions(elf_file);
    bool clear_at_end = false;
    sel4gpi_elf_region_t stack_regions[num_regions];
    if (regions == NULL)
    {
        regions = stack_regions;
        clear_at_end = true;
    }

    /* Create reservations */
    int error = elf_reserve_regions_in_ads(loadee, elf_file, num_regions, regions, mapanywhere);
    if (error)
    {
        ZF_LOGE("Failed to reserve regions");
        return NULL;
    }

    /* Load Map reservations and load in elf data */
    error = load_segments(loadee, loader, loadee_vka, loader_vka, elf_file, num_regions, regions);
    if (error)
    {
        ZF_LOGE("Failed to load segments");
        return NULL;
    }

    /* Return entry point */
    return entry_point(elf_file);
}

uintptr_t sel4gpi_elf_get_vsyscall(const elf_t *elf_file)
{
    uintptr_t *addr = (uintptr_t *)sel4gpi_elf_get_section(elf_file, "__vsyscall", NULL);
    if (addr)
    {
        return *addr;
    }
    else
    {
        return 0;
    }
}

uintptr_t sel4gpi_elf_get_section(const elf_t *elf_file, const char *section_name, uint64_t *section_size)
{
    /* See if we can find the section */
    size_t section_id;
    const void *addr = elf_getSectionNamed(elf_file, section_name, &section_id);
    if (addr)
    {
        if (section_size != NULL)
        {
            *section_size = elf_getSectionSize(elf_file, section_id);
        }
        return (uintptr_t)addr;
    }
    else
    {
        return 0;
    }
}

void *sel4gpi_elf_load(ads_t *loadee, ads_t *loader, vka_t *loadee_vka, vka_t *loader_vka,
                       const elf_t *elf_file)
{
    return sel4gpi_elf_load_record_regions(loadee, loader, loadee_vka, loader_vka, elf_file, NULL, 0);
}

uint32_t sel4gpi_elf_num_phdrs(const elf_t *elf_file)
{
    return elf_getNumProgramHeaders(elf_file);
}

void sel4gpi_elf_read_phdrs(const elf_t *elf_file, size_t max_phdrs, Elf_Phdr *phdrs)
{
    size_t num_phdrs = elf_getNumProgramHeaders(elf_file);
    for (size_t i = 0; i < num_phdrs && i < max_phdrs; i++)
    {
        phdrs[i] = (Elf_Phdr){
            .p_type = elf_getProgramHeaderType(elf_file, i),
            .p_offset = elf_getProgramHeaderOffset(elf_file, i),
            .p_vaddr = elf_getProgramHeaderVaddr(elf_file, i),
            .p_paddr = elf_getProgramHeaderPaddr(elf_file, i),
            .p_filesz = elf_getProgramHeaderFileSize(elf_file, i),
            .p_memsz = elf_getProgramHeaderMemorySize(elf_file, i),
            .p_flags = elf_getProgramHeaderFlags(elf_file, i),
            .p_align = elf_getProgramHeaderAlign(elf_file, i)};
    }
}
