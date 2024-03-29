cmake_minimum_required(VERSION 3.7.2)
project(libsel4vmm C CXX ASM)

# Use arm arch source files on RISCV
set(arch ${KernelArch})
if(KernelArchRiscV)
    set(arch arm)
endif()

if(KernelPlatform STREQUAL "qemu-arm-virt")
    set(KernelPlatVMM "qemu_arm_virt")
endif()

find_program(DTC_TOOL dtc)
    if("${DTC_TOOL}" STREQUAL "DTC_TOOL-NOTFOUND")
        message(FATAL_ERROR "Cannot find 'dtc' program.")
    endif()

find_program(LD_LINKER ld)
    if("${LD_LINKER}" STREQUAL "LD-LINKER-NOTFOUND")
        message(FATAL_ERROR "Cannot find 'ld' program.")
    endif()

set(
    VmDTSPath "${CMAKE_CURRENT_SOURCE_DIR}/board/${KernelPlatVMM}/linux.dts" # TODO: don't hardcode this
    CACHE INTERNAL "Location of VM DTS file"
    )

set(
    VmDTBPath "${CMAKE_CURRENT_BINARY_DIR}/linux.dtb"
)

# Compile DTS to DTB
execute_process(
    COMMAND
        ${DTC_TOOL} -I dts -O dtb -o ${VmDTBPath} ${VmDTSPath}
    RESULT_VARIABLE error
)
if(error)
    message(FATAL_ERROR "Failed to compile DTS to DTB: ${VmDTSPath}")
endif()

file(
    GLOB
        deps
        src/*.c
        src/arch/${KernelSel4Arch}/*.c
        src/arch/${KernelSel4Arch}/vgic/*.c
)

string(CONCAT package_guest_image_defs
    "GUEST_KERNEL_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/board/${KernelPlatVMM}/linux\"\;"
    "GUEST_DTB_IMAGE_PATH=\"${VmDTBPath}\"\;"
    "GUEST_INITRD_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/board/${KernelPlatVMM}/rootfs.cpio.gz\"\;")

set_source_files_properties(
    tools/package_guest_images.S
    PROPERTIES
    COMPILE_OPTIONS "-x;assembler-with-cpp;"
    COMPILE_DEFINITIONS ${package_guest_image_defs}
)

list(
    APPEND
    deps
    ${VmDTBPath}
    tools/package_guest_images.S
)

list(
    REMOVE_ITEM
    deps
    ${CMAKE_CURRENT_SOURCE_DIR}/src/arch/${KernelSel4Arch}/vgic/vgic_v3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/arch/${KernelSel4Arch}/vcpu.c
)

# add_link_options("-fuse-ld=lld")

# add_executable(vmm EXCLUDE_FROM_ALL ${deps})

add_library(sel4vmm STATIC EXCLUDE_FROM_ALL ${deps})

target_include_directories(
    sel4vmm
    PUBLIC include arch/${KernelSel4Arch}
)

target_link_libraries(
    sel4vmm
    PUBLIC 
        muslc 
        sel4 
        sel4_autoconf 
        sel4utils 
        sel4vka 
        sel4allocman 
        sel4muslcsys
)

target_compile_options(sel4vmm PUBLIC  "-DBOARD_${KernelPlatVMM}")
