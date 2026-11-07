# RISC-V (for CH32V203). Replace VERSION with the one in the file name you already


# downloaded: xpack-riscv-none-elf-gcc-<VERSION>-linux-x64.tar.gz
set(RISCV_VERSION "15.2.0-1")
set(RISCV_FILE "xpack-riscv-none-elf-gcc-${RISCV_VERSION}-linux-x64.tar.gz")
set(RISCV_URL  "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${RISCV_VERSION}/${RISCV_FILE}")
set(RISCV_SHA256 "")     