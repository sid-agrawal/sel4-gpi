cmake_minimum_required(VERSION 3.7.2)
project(vmm C CXX ASM)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

set(GPIVMMImplementation "sel4test-vmm" 
    CACHE STRING 
    "The VMM implemenation to use: either sel4test-vmm or osm-vmm")

if (GPIVMMImplementation STREQUAL "sel4test-vmm")
    set(VMMImplDef "SEL4TEST_VMM")
elseif(GPIVMMImplementation STREQUAL "osm-vmm")
    set(VMMImplDef "OSM_VMM")
endif()


if(KernelPlatform STREQUAL "qemu-arm-virt")
    set(KernelPlatVMM "qemu_arm_virt")
elseif(KernelPlatform STREQUAL "odroidc4")
    set(KernelPlatVMM "${KernelPlatform}")
endif()

find_program(DTC_TOOL dtc)
    if("${DTC_TOOL}" STREQUAL "DTC_TOOL-NOTFOUND")
        message(FATAL_ERROR "Cannot find 'dtc' program.")
    endif()

set(
    VmDTSPath "${CMAKE_CURRENT_SOURCE_DIR}/board/${KernelPlatVMM}/linux.dts"
)

set(
    VmDTBPath "${CMAKE_CURRENT_BINARY_DIR}/linux.dtb"
)

# Compile DTS to DTB
execute_process(
    COMMAND
        ${DTC_TOOL} -q -I dts -O dtb -o ${VmDTBPath} ${VmDTSPath}
    RESULT_VARIABLE error
)
if(error)
    message(FATAL_ERROR "Failed to compile DTS to DTB: ${VmDTSPath}")
endif()

file(
    GLOB
        deps
        src/*.c
        src/${GPIVMMImplementation}/*.c
        src/arch/${KernelSel4Arch}/*.c
        # src/arch/${KernelSel4Arch}/${GPIVMMImplementation}/*.c
        # src/arch/${KernelSel4Arch}/${GPIVMMImplementation}/vgic/*.c
        # src/arch/${KernelSel4Arch}/vgic/*.c
)

string(CONCAT package_guest_image_defs
    "GUEST_KERNEL_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/board/${KernelPlatVMM}/linux\"\;"
    "GUEST_DTB_IMAGE_PATH=\"${VmDTBPath}\"\;"
    "GUEST_INITRD_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/board/${KernelPlatVMM}/rootfs.cpio.gz\"\;"
)

set_source_files_properties(
    tools/package_guest_images.S
    PROPERTIES
    COMPILE_OPTIONS "-x;assembler-with-cpp;"
    COMPILE_DEFINITIONS ${package_guest_image_defs}
)

list(
    APPEND
    deps
    ${VmDTSPath}
    tools/package_guest_images.S
    ${CMAKE_CURRENT_SOURCE_DIR}/board/${KernelPlatVMM}/bootimg.bin
)

# None of the platforms we have access to support VGIC v3
list(
    REMOVE_ITEM
    deps
    ${CMAKE_CURRENT_SOURCE_DIR}/src/arch/${KernelSel4Arch}/${GPIVMMImplementation}/vgic/vgic_v3.c
)

# add_link_options("-fuse-ld=lld")

# add_executable(vmm EXCLUDE_FROM_ALL ${deps})

add_library(vmm STATIC EXCLUDE_FROM_ALL ${deps})

target_include_directories(
    vmm
    PUBLIC 
        include
        arch/${KernelSel4Arch}
        arch/${KernelSel4Arch}/${GPIVMMImplementation}
)

target_link_libraries(
    vmm
    PUBLIC 
        muslc 
        sel4 
        sel4_autoconf 
        sel4utils 
        sel4vka 
        sel4allocman 
        sel4muslcsys
)

target_compile_options(vmm PUBLIC  
    "-DBOARD_${KernelPlatVMM}" 
    "-DSMOLDTB_STATIC_BUFFER_SIZE=92160" 
    "-D${VMMImplDef}")
