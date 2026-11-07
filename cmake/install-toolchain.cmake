#usage: cmake -DTOOL=riscv -P cmake/install-toolchain.cmake

cmake_minimum_required(VERSION 3.18)
include(${CMAKE_CURRENT_LIST_DIR}/toolchains.cmake)

if(NOT TOOL)
    message(FATAL_ERROR "Usage: cmake -DTOOL=riscv -P cmake/install-toolchain.cmake")
endif()

string(TOUPPER "${TOOL}" T)
if(NOT DEFINED ${T}_URL)
    message(FATAL_ERROR "Unknown tool: '${TOOL}' (see cmake/toolchains.cmake)")
endif()

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(TOOLS "${ROOT}/tools")
set(DEST "${TOOLS}/${TOOL}-gcc")
set(MARK "${DEST}/.installed-${${T}_VERSION}")


if("${ACTION}" STREQUAL "remove" OR "${ACTION}" STREQUAL "purge")
  file(REMOVE_RECURSE "${DEST}" "${TOOLS}/extract-tmp")
  if("${ACTION}" STREQUAL "purge")
    file(REMOVE_RECURSE "${TOOLS}/download")
  endif()
  message(STATUS "${ACTION}: ${TOOL} toolchain removed from ${DEST}")
  return()
endif()

if(EXISTS "${MARK}")
    message(STATUS "${TOOL} toolchain ${${T}_VERSION} already installed in ${DEST}")
    return()
endif()

set(ARCHIVE "${TOOLS}/download/${${T}_FILE}")
file(MAKE_DIRECTORY "${TOOLS}/download")

if(${T}_SHA256)
  file(DOWNLOAD "${${T}_URL}" "${ARCHIVE}" EXPECTED_HASH SHA256=${${T}_SHA256} SHOW_PROGRESS STATUS st)
else()
  file(DOWNLOAD "${${T}_URL}" "${ARCHIVE}" SHOW_PROGRESS STATUS st)
endif()

list(GET st 0 code)
if(code)
  list(GET st 1 msg)
  message(FATAL_ERROR "Download failed: ${msg}")
endif()

if(NOT ${T}_SHA256)
  file(SHA256 "${ARCHIVE}" got)
  message(WARNING "No hash pinned yet. Put this line in cmake/toolchains.cmake and run again:\n  set(${T}_SHA256 \"${got}\")")
endif()

set(TMP "${TOOLS}/extract-tmp")
file(REMOVE_RECURSE "${TMP}" "${DEST}")
file(MAKE_DIRECTORY "${TMP}")

# unpack the .tar.gz into the temp folder. It creates one top-level folder inside, named like xpack-riscv-none-elf-gcc-15.2.0-1.
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${TMP}")


file(GLOB top LIST_DIRECTORIES true "${TMP}/*")
list(GET top 0 top0)
file(RENAME "${top0}" "${DEST}")

file(REMOVE_RECURSE "${TMP}")
file(WRITE "${MARK}" "")
message(STATUS "Installed ...")