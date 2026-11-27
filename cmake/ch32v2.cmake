get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# WCH GCC 12.2 (prefix riscv-wch-elf-). Install it with:
#   cmake -DTOOL=riscv -P cmake/install-toolchain.cmake
# Local copy first, then whatever is in PATH.
find_program(RISCV_GCC
  NAMES riscv-wch-elf-gcc riscv-none-elf-gcc riscv64-unknown-elf-gcc
  PATHS "${_root}/tools/riscv-gcc/bin"
  NO_DEFAULT_PATH
)

if(NOT RISCV_GCC)
  find_program(RISCV_GCC NAMES riscv-wch-elf-gcc riscv-none-elf-gcc riscv64-unknown-elf-gcc)
endif()

if(NOT RISCV_GCC)
  message(FATAL_ERROR "riscv-wch-elf-gcc not found.\n"
    "Host: ${CMAKE_HOST_SYSTEM_NAME}\n"
    "Looked in: ${_root}/tools/riscv-gcc/bin and PATH\n"
    "Run: cmake -DTOOL=riscv -P cmake/install-toolchain.cmake")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

set(CMAKE_C_COMPILER   ${RISCV_GCC})
set(CMAKE_ASM_COMPILER ${RISCV_GCC})

get_filename_component(RISCV_BIN_DIR "${RISCV_GCC}" DIRECTORY)
get_filename_component(_riscv_gcc_name "${RISCV_GCC}" NAME)
string(REGEX REPLACE "gcc$" "" _riscv_tool_prefix "${_riscv_gcc_name}")
set(CMAKE_OBJCOPY "${RISCV_BIN_DIR}/${_riscv_tool_prefix}objcopy" CACHE FILEPATH "RISC-V objcopy")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-march=rv32imac_zicsr_zifencei -mabi=ilp32")
set(CMAKE_ASM_FLAGS_INIT "-march=rv32imac_zicsr_zifencei -mabi=ilp32")