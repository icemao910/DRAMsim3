cmake_minimum_required(VERSION 3.16)
project(dramsim3)

set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # Use libc++ for compilation; do NOT force -lc++ or -lc++abi here so the compiler driver
  # will pick the matching runtime. Setting linker flags manually can cause mismatches.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()

add_library(inih INTERFACE)
target_include_directories(inih INTERFACE ext/headers)

add_library(format INTERFACE)
target_include_directories(format INTERFACE ext/fmt/include)
target_compile_definitions(format INTERFACE FMT_HEADER_ONLY=1 FMT_USE_CHAR8_T=0)

# argparsing library, only used in main program not the library
add_library(args INTERFACE)
target_include_directories(args INTERFACE ext/headers)

add_library(json INTERFACE)
target_include_directories(json INTERFACE ext/headers)

# Main DRAMSim Lib
# collect sources from src/ and add them to the shared library target
file(GLOB_RECURSE DRAMSIM3_SRC "${PROJECT_SOURCE_DIR}/src/*.cc")

# Exclude thermal sources when THERMAL is not enabled (they require THERMAL-specific Config fields)
if(NOT THERMAL)
  list(REMOVE_ITEM DRAMSIM3_SRC
    "${PROJECT_SOURCE_DIR}/src/thermal.cc"
    "${PROJECT_SOURCE_DIR}/src/sp_ienv.c"
    "${PROJECT_SOURCE_DIR}/src/thermal_solver.c"
    "${PROJECT_SOURCE_DIR}/src/thermal_replay.cc"
  )
endif()

add_library(dramsim3 SHARED ${DRAMSIM3_SRC})

# If using Homebrew/LLVM clang++ (compiler next to Homebrew libs), link its libc++ and libc++abi
if(CMAKE_CXX_COMPILER)
  get_filename_component(_cc_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(_candidate_lib_dir "${_cc_dir}/../lib")
  file(TO_CMAKE_PATH "${_candidate_lib_dir}" _candidate_lib_dir)
  if(EXISTS "${_candidate_lib_dir}/libc++.dylib" AND EXISTS "${_candidate_lib_dir}/libc++abi.dylib")
    message(STATUS "Linking libc++ and libc++abi from ${_candidate_lib_dir}")
    target_link_libraries(dramsim3 PRIVATE
      "${_candidate_lib_dir}/libc++.dylib"
      "${_candidate_lib_dir}/libc++abi.dylib"
    )
    if(APPLE)
      target_link_options(dramsim3 PRIVATE "-Wl,-rpath,${_candidate_lib_dir}")
    endif()
  endif()
endif()

if (THERMAL)
    # dependency check
    # sudo apt-get install libatlas-base-dev on ubuntu
    find_package(BLAS REQUIRED)
    find_package(OpenMP REQUIRED)
    # YOU need to build superlu on your own. Do the following:
    # git submodule update --init
    # cd ext/SuperLU_MT_3.1 && make lib
    find_library(SUPERLU
        NAME superlu_mt_OPENMP libsuperlu_mt_OPENMP
        HINTS ${PROJECT_SOURCE_DIR}/ext/SuperLU_MT_3.1/lib/
    )

    target_link_libraries(dramsim3
        PRIVATE ${SUPERLU} f77blas atlas m ${OpenMP_C_FLAGS}
    )
    target_sources(dramsim3
        PRIVATE src/thermal.cc src/sp_ienv.c src/thermal_solver.c
    )
    target_compile_options(dramsim3 PRIVATE -DTHERMAL -D_LONGINT -DAdd_ ${OpenMP_C_FLAGS})

    add_executable(thermalreplay src/thermal_replay.cc)
    target_link_libraries(thermalreplay dramsim3 inih)
    target_compile_options(thermalreplay PRIVATE -DTHERMAL -D_LONGINT -DAdd_ ${OpenMP_C_FLAGS})
endif (THERMAL)

if (CMD_TRACE)
    target_compile_options(dramsim3 PRIVATE -DCMD_TRACE)
endif (CMD_TRACE)

if (ADDR_TRACE)
    target_compile_options(dramsim3 PRIVATE -DADDR_TRACE)
endif (ADDR_TRACE)


target_include_directories(dramsim3 INTERFACE src)
target_compile_options(dramsim3 PRIVATE -Wall)
target_link_libraries(dramsim3 PRIVATE inih format)
set_target_properties(dramsim3 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# If using Homebrew/opt/llvm clang++, link its libc++ and libc++abi to avoid
# header/runtime mismatches (resolves undefined symbols like std::__1::__hash_memory).
if(CMAKE_CXX_COMPILER)
  # compute candidate lib dir next to the compiler binary and follow symlinks
  get_filename_component(_clang_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(_clang_realpath "${CMAKE_CXX_COMPILER}" REALPATH)
  get_filename_component(_clang_real_dir "${_clang_realpath}" DIRECTORY)

  # candidate locations to search for libc++/libc++abi
  set(_candidates
    "${_clang_bin_dir}/../lib"
    "${_clang_real_dir}/../lib"
    "/opt/homebrew/Cellar/llvm/*/lib"
    "/opt/homebrew/opt/llvm/lib"
    "/opt/homebrew/lib"
  )

  foreach(_cand IN LISTS _candidates)
    file(GLOB _cxx_libs RELATIVE "${_cand}" "${_cand}/libc++.dylib" "${_cand}/libc++*.dylib")
    if(_cxx_libs)
      # pick the directory containing the first match (use absolute path)
      set(_homebrew_lib_dir "${_cand}")
      set(_libcxx "${_homebrew_lib_dir}/libc++.dylib")
      set(_libcxxabi "${_homebrew_lib_dir}/libc++abi.dylib")

      if(EXISTS "${_libcxx}" AND EXISTS "${_libcxxabi}")
        message(STATUS "Linking Homebrew libc++ and libc++abi from ${_homebrew_lib_dir}")

        # Link the dylibs by full path to avoid the system linker picking the wrong runtime.
        target_link_libraries(dramsim3 PRIVATE
          "${_libcxx}"
          "${_libcxxabi}"
        )
        set(_linked_homebrew_lib TRUE)

        # Ensure the runtime loader will find the Homebrew libs at run/link time.
        # Add rpath to the library target and executables that link it.
        if (APPLE)
          target_link_options(dramsim3 PRIVATE "-Wl,-rpath,${_homebrew_lib_dir}")
          # also ensure executables get the same rpath
          target_link_options(dramsim3main PRIVATE "-Wl,-rpath,${_homebrew_lib_dir}")
          if (TARGET dramsim3test)
            target_link_options(dramsim3test PRIVATE "-Wl,-rpath,${_homebrew_lib_dir}")
          endif()
        endif()

        break()
      endif()
    endif()
  endforeach()

  # Fallback: explicitly try common Homebrew opt locations if the glob above didn't match.
  if(NOT _linked_homebrew_lib)
    set(_fallback_libs
      "/opt/homebrew/opt/llvm/lib/libc++.dylib"
      "/opt/homebrew/opt/llvm/lib/libc++abi.dylib"
      "/opt/homebrew/lib/libc++.dylib"
      "/opt/homebrew/lib/libc++abi.dylib"
    )

    # CMake lists don't support [0] indexing; use list(GET ...) instead.
    list(GET _fallback_libs 0 _fb0)
    list(GET _fallback_libs 1 _fb1)
    list(GET _fallback_libs 2 _fb2)
    list(GET _fallback_libs 3 _fb3)

    if(EXISTS "${_fb0}" AND EXISTS "${_fb1}")
      message(STATUS "Linking Homebrew libc++ and libc++abi from /opt/homebrew/opt/llvm/lib")
      target_link_libraries(dramsim3 PRIVATE "${_fb0}" "${_fb1}")
      if(APPLE)
        target_link_options(dramsim3 PRIVATE "-Wl,-rpath,/opt/homebrew/opt/llvm/lib")
        target_link_options(dramsim3main PRIVATE "-Wl,-rpath,/opt/homebrew/opt/llvm/lib")
        if (TARGET dramsim3test)
          target_link_options(dramsim3test PRIVATE "-Wl,-rpath,/opt/homebrew/opt/llvm/lib")
        endif()
      endif()
      set(_linked_homebrew_lib TRUE)
    elseif(EXISTS "${_fb2}" AND EXISTS "${_fb3}")
      message(STATUS "Linking Homebrew libc++ and libc++abi from /opt/homebrew/lib")
      target_link_libraries(dramsim3 PRIVATE "${_fb2}" "${_fb3}")
      if(APPLE)
        target_link_options(dramsim3 PRIVATE "-Wl,-rpath,/opt/homebrew/lib")
        target_link_options(dramsim3main PRIVATE "-Wl,-rpath,/opt/homebrew/lib")
        if (TARGET dramsim3test)
          target_link_options(dramsim3test PRIVATE "-Wl,-rpath,/opt/homebrew/lib")
        endif()
      endif()
      set(_linked_homebrew_lib TRUE)
    endif()
  endif()
endif()

# trace CPU, .etc
add_executable(dramsim3main src/main.cc src/cpu.cc)
target_link_libraries(dramsim3main PRIVATE dramsim3 args)
target_compile_options(dramsim3main PRIVATE)
set_target_properties(dramsim3main PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# Unit testing
add_library(Catch INTERFACE)
target_include_directories(Catch INTERFACE ext/headers)

add_executable(dramsim3test EXCLUDE_FROM_ALL
    tests/test_config.cc
    tests/test_dramsys.cc
    tests/test_hmcsys.cc # IDK somehow this can literally crush your computer
)
target_link_libraries(dramsim3test Catch dramsim3)
target_include_directories(dramsim3test PRIVATE src/)

# We have to use this custome command because there's a bug in cmake
# that if you do `make test` it doesn't build your updated test files
# so we're stucking with `make dramsim3test` for now
add_custom_command(
    TARGET dramsim3test POST_BUILD
    COMMAND dramsim3test
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    DEPENDS dramsim3test dramsim3
)

# Use C++17 (fmt ext expects std::result_of). Keep flags so fmt doesn't use char8_t
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Make sure libc++ re-enables removed char_traits and fmt is instructed to avoid char8_t
add_compile_definitions(_LIBCPP_ENABLE_CXX20_REMOVED_CHAR_TRAITS=1)
foreach(tgt dramsim3 dramsim3main)
  if (TARGET ${tgt})
    set_target_properties(${tgt} PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED YES
      CXX_EXTENSIONS NO
    )
    target_compile_definitions(${tgt} PRIVATE _LIBCPP_ENABLE_CXX20_REMOVED_CHAR_TRAITS=1 FMT_USE_CHAR8_T=0)
  endif()
endforeach()

# ensure fmt char8_t trait fix is included first
if(APPLE)
  set(FMT_CHAR_TRAITS_FIX "${CMAKE_SOURCE_DIR}/ext/fmt/fmt_char_traits_fix.h")
  target_compile_options(dramsim3 PRIVATE -include "${FMT_CHAR_TRAITS_FIX}")
  target_compile_options(dramsim3main PRIVATE -include "${FMT_CHAR_TRAITS_FIX}")
endif()
