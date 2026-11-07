get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

find_program(RISCV_GCC riscv-none-elf-gcc PATHS "${_root}/tools/riscv-gcc/bin" NO_DEFAULT_PATH)

if(NOT RISCV_GCC)
  find_program(RISCV_GCC riscv-none-elf-gcc)
endif()

if(NOT RISCV_GCC)
  message(FATAL_ERROR "riscv-none-elf-gcc not found. Run: ...")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

set(CMAKE_C_COMPILER   ${RISCV_GCC})
set(CMAKE_ASM_COMPILER ${RISCV_GCC})

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-march=rv32imac_zicsr -mabi=ilp32")
set(CMAKE_ASM_FLAGS_INIT "-march=rv32imac_zicsr -mabi=ilp32")
