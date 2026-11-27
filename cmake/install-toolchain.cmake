# Install / remove a cross toolchain described in cmake/toolchains.cmake
#
#   cmake -DTOOL=riscv                    -P cmake/install-toolchain.cmake   install
#   cmake -DTOOL=riscv -DACTION=remove    -P cmake/install-toolchain.cmake   delete the extracted toolchain (keeps the download)
#   cmake -DTOOL=riscv -DACTION=purge     -P cmake/install-toolchain.cmake   delete toolchain AND the downloaded archive
#   cmake -DTOOL=riscv -DACTION=reinstall -P cmake/install-toolchain.cmake   purge, then download + install from scratch

cmake_minimum_required(VERSION 3.18)
include(${CMAKE_CURRENT_LIST_DIR}/toolchains.cmake)

if(NOT TOOL)
  message(FATAL_ERROR
    "Usage: cmake -DTOOL=riscv [-DACTION=remove|purge|reinstall] -P cmake/install-toolchain.cmake")
endif()

string(TOUPPER "${TOOL}" T)
if(NOT DEFINED ${T}_URL)
  message(FATAL_ERROR "Unknown tool: '${TOOL}' (see cmake/toolchains.cmake)")
endif()

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(TOOLS   "${ROOT}/tools")
set(DEST    "${TOOLS}/${TOOL}-gcc")
set(MARK    "${DEST}/.installed-${${T}_VERSION}")
set(URL     "${${T}_URL}")
set(WANT    "${${T}_SHA256}")
set(ARCHIVE "${TOOLS}/download/${${T}_FILE}")
set(PART    "${ARCHIVE}.part")      # downloads go here first, renamed only when complete + verified
set(TMP     "${TOOLS}/extract-tmp")
string(TOLOWER "${WANT}" WANT)

# ---------------------------------------------------------------- remove / purge / reinstall
if("${ACTION}" STREQUAL "remove" OR "${ACTION}" STREQUAL "purge" OR "${ACTION}" STREQUAL "reinstall")
  file(REMOVE_RECURSE "${DEST}" "${TMP}")
  if(NOT "${ACTION}" STREQUAL "remove")
    file(REMOVE_RECURSE "${TOOLS}/download")
  endif()
  message(STATUS "${ACTION}: ${TOOL} toolchain removed (${DEST})")
  if(NOT "${ACTION}" STREQUAL "reinstall")
    return()
  endif()
elseif(NOT "${ACTION}" STREQUAL "")
  message(FATAL_ERROR "Unknown ACTION '${ACTION}' (use remove, purge or reinstall)")
endif()

# ---------------------------------------------------------------- already installed?
if(EXISTS "${MARK}")
  message(STATUS "${TOOL} toolchain ${${T}_VERSION} already installed in ${DEST}")
  return()
endif()

# ---------------------------------------------------------------- get a GOOD archive
file(MAKE_DIRECTORY "${TOOLS}/download")
file(REMOVE "${PART}")   # a .part file is a leftover of an interrupted run: never trust it

set(need_download TRUE)
if(EXISTS "${ARCHIVE}")
  file(SIZE "${ARCHIVE}" sz)
  set(good TRUE)
  if(sz EQUAL 0)
    set(good FALSE)
    message(STATUS "Existing archive is empty -> deleting it")
  elseif(NOT "${WANT}" STREQUAL "")
    message(STATUS "Verifying existing archive (SHA256)...")
    file(SHA256 "${ARCHIVE}" have)
    if(NOT "${have}" STREQUAL "${WANT}")
      set(good FALSE)
      message(STATUS "Existing archive is corrupt or incomplete (hash mismatch) -> deleting it")
    endif()
  endif()
  if(good)
    message(STATUS "Using already downloaded file: ${ARCHIVE}")
    set(need_download FALSE)
  else()
    file(REMOVE "${ARCHIVE}")
  endif()
endif()

if(need_download)
  message(STATUS "Downloading ${URL}")
  message(STATUS "(~600 MB; GitHub builds this archive on the fly, so it may take a few minutes)")
  find_program(CURL_EXE curl)
  if(CURL_EXE)
    # curl gives a live progress meter, retries, and aborts if the speed stays under 1 KB/s for 60 s
    execute_process(
      COMMAND "${CURL_EXE}" -L --fail --retry 3 --connect-timeout 30
              --speed-time 60 --speed-limit 1024 --progress-bar -o "${PART}" "${URL}"
      RESULT_VARIABLE rc)
    if(rc)
      file(REMOVE "${PART}")
      message(FATAL_ERROR "Download failed (curl exit code ${rc}). Nothing was kept, just run again.")
    endif()
  else()
    file(DOWNLOAD "${URL}" "${PART}" SHOW_PROGRESS INACTIVITY_TIMEOUT 60 TIMEOUT 3600 STATUS st)
    list(GET st 0 code)
    if(code)
      list(GET st 1 msg)
      file(REMOVE "${PART}")
      message(FATAL_ERROR "Download failed: ${msg}. Nothing was kept, just run again.")
    endif()
  endif()

  file(SIZE "${PART}" sz)
  if(sz EQUAL 0)
    file(REMOVE "${PART}")
    message(FATAL_ERROR "Downloaded file is empty. Nothing was kept, just run again.")
  endif()

  file(SHA256 "${PART}" have)
  if("${WANT}" STREQUAL "")
    message(WARNING "No hash pinned yet. Put this line in cmake/toolchains.cmake:\n  set(${T}_SHA256 \"${have}\")")
  elseif(NOT "${have}" STREQUAL "${WANT}")
    file(REMOVE "${PART}")
    message(FATAL_ERROR
      "SHA256 mismatch, download deleted.\n  expected: ${WANT}\n  got:      ${have}\n"
      "Either the download was damaged (run again), or GitHub regenerated the archive "
      "(then update ${T}_SHA256 in cmake/toolchains.cmake with the 'got' value).")
  endif()

  file(RENAME "${PART}" "${ARCHIVE}")   # only a complete, verified file ever gets the real name
endif()

# ---------------------------------------------------------------- extract
file(REMOVE_RECURSE "${TMP}" "${DEST}")
file(MAKE_DIRECTORY "${TMP}")

# The .tar.gz holds one top-level folder (<repo>-<tag>); it is renamed to DEST below.
message(STATUS "Extracting (no progress bar, this can take a few minutes)...")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${TMP}")

file(GLOB top LIST_DIRECTORIES true "${TMP}/*")
list(LENGTH top ntop)
if(ntop EQUAL 0)
  file(REMOVE_RECURSE "${TMP}")
  file(REMOVE "${ARCHIVE}")
  message(FATAL_ERROR "Archive extracted to nothing, so it was bad. It was deleted, run again.")
endif()
list(GET top 0 top0)
file(RENAME "${top0}" "${DEST}")

# sanity check: the compiler must be in <DEST>/bin (catches an unexpected archive layout)
file(GLOB _gcc "${DEST}/bin/riscv-*-gcc*")
if(NOT _gcc)
  message(FATAL_ERROR "Unexpected archive layout: no bin/riscv-*-gcc inside ${DEST}")
endif()

file(REMOVE_RECURSE "${TMP}")
file(WRITE "${MARK}" "")

# smoke test: does the compiler actually start on this machine?
file(GLOB _gcc_main "${DEST}/bin/riscv-wch-elf-gcc" "${DEST}/bin/riscv-wch-elf-gcc.exe")
if(_gcc_main)
  execute_process(COMMAND ${_gcc_main} --version OUTPUT_VARIABLE ver RESULT_VARIABLE rc ERROR_QUIET)
  if(rc)
    message(WARNING "Installed, but the compiler did not start (exit code ${rc}).")
  else()
    string(REGEX MATCH "^[^\n]*" ver1 "${ver}")
    message(STATUS "Compiler OK: ${ver1}")
  endif()
endif()
message(STATUS "Installed ${TOOL} toolchain ${${T}_VERSION} in ${DEST}")