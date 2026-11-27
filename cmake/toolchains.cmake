# RISC-V toolchain for CH32V203: WCH GCC 12.2.0 (compiler prefix: riscv-wch-elf-)
# Hosted on GitHub by TianpeiLee. install-toolchain.cmake reads RISCV_VERSION / RISCV_URL /
# RISCV_FILE / RISCV_SHA256 from this file. The right package is chosen from the computer
# that runs the install (Linux x86_64 or Windows x86_64). WSL counts as Linux.
set(RISCV_VERSION "12.2.0")
set(_wch_base "https://github.com/TianpeiLee/riscv-gun-toolchain-12.2.0")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(_wch_repo "${_wch_base}-x86_64-w64-mingw32-riscv-wch-elf")
  set(_wch_os   "windows-x64")
  # Windows: leave empty, run the install once, then paste the hash it prints
  set(RISCV_SHA256 "")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(_wch_repo "${_wch_base}-x86_64-linux-riscv-wch-elf")
  set(_wch_os   "linux-x64")
  set(RISCV_SHA256 "8b9c27b9631c24e91fd51ca496ccd08f66832191d400848112ee4b9e0f8c4c39")
else()
  message(FATAL_ERROR "No WCH RISC-V toolchain package for host '${CMAKE_HOST_SYSTEM_NAME}'. "
                      "Use Linux (or WSL) or Windows x86_64.")
endif()

# GitHub "source archive" of the tag (NOT the /releases/tag/... web page)
set(RISCV_URL  "${_wch_repo}/archive/refs/tags/${RISCV_VERSION}.tar.gz")
# Own file name per OS: both URLs end in the same "12.2.0.tar.gz"
set(RISCV_FILE "riscv-wch-elf-${RISCV_VERSION}-${_wch_os}.tar.gz")

# NOTE: do NOT add another set(RISCV_SHA256 ...) down here. It would override the per-OS values above.