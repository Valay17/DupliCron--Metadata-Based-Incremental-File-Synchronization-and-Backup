cmake_minimum_required(VERSION 3.13)
project(DupliCron VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_STATIC_RUNTIME "Link C++ runtime libraries statically" OFF)

if(MSVC)
    if(USE_STATIC_RUNTIME)
        message(STATUS "Using static MSVC runtime (/MT)...")
        foreach(config "" "_DEBUG" "_RELEASE" "_RELWITHDEBINFO" "_MINSIZEREL")
            foreach(lang "C" "CXX")
                string(REPLACE "/MD" "/MT" CMAKE_${lang}_FLAGS${config} "${CMAKE_${lang}_FLAGS${config}}")
            endforeach()
        endforeach()
    else()
        message(STATUS "Using dynamic MSVC runtime (/MD) [default]...")
        # Leave as is, dynamic is default in MSVC
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(USE_STATIC_RUNTIME)
        message(STATUS "Using static linking for libstdc++ and libgcc...")
        foreach(config "" "_DEBUG" "_RELEASE" "_RELWITHDEBINFO" "_MINSIZEREL")
            set(CMAKE_CXX_FLAGS${config} "${CMAKE_CXX_FLAGS${config}} -static-libstdc++ -static-libgcc")
            set(CMAKE_C_FLAGS${config} "${CMAKE_C_FLAGS${config}} -static-libgcc")
        endforeach()
    else()
        message(STATUS "Using dynamic linking for libstdc++ and libgcc (default)...")
        # Default, do nothing
    endif()
endif()

# Output Directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${cfg})
endforeach()

add_custom_target(create_runtime_dirs ALL
    COMMENT "Create Meta_Cache and Sync_Logs, Copying Config.txt directories next to executables"
)

if(WIN32)
    add_custom_command(TARGET create_runtime_dirs
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DupliCron>/Meta_Cache"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DupliCron>/Sync_Logs"
        COMMAND cmd /c "if not exist \"$<TARGET_FILE_DIR:DupliCron>\\Config.txt\" copy \"${CMAKE_SOURCE_DIR}\\Config.txt\" \"$<TARGET_FILE_DIR:DupliCron>\\Config.txt\""
    )
else()
    add_custom_command(TARGET create_runtime_dirs
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DupliCron>/Meta_Cache"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DupliCron>/Sync_Logs"
        COMMAND /bin/sh -c "[ -f \"$<TARGET_FILE_DIR:DupliCron>/Config.txt\" ] ||cp"${CMAKE_SOURCE_DIR}/Config.txt $<TARGET_FILE_DIR:DupliCron>/Config.txt""
# Do not edit the cp command line, it is not a whitespace error, adding space makes it unrecognizable by bash
    )
endif()

# Source Files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

file(GLOB BLAKE3_SOURCES CONFIGURE_DEPENDS "include/Blake3/*.c")

add_compile_definitions(
    BLAKE3_NO_SSE2
    BLAKE3_NO_AVX2
    BLAKE3_NO_AVX512
    BLAKE3_NO_NEON
    BLAKE3_NO_SSE41
)

add_executable(DupliCron ${SOURCES} ${BLAKE3_SOURCES})

# Include paths for headers (Quill, Blake3, your own headers)
target_include_directories(DupliCron PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(UNIX)
    target_link_libraries(DupliCron pthread)
endif()

# Packaging -------------------------------
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/package" CACHE PATH "Install path" FORCE)

install(TARGETS DupliCron DESTINATION DupliCron)
install(FILES "${CMAKE_SOURCE_DIR}/Config.txt" DESTINATION DupliCron)
install(CODE "
  file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/DupliCron/Meta_Cache\")
  file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/DupliCron/Sync_Logs\")
")

set(CPACK_PACKAGE_NAME "DupliCron")
set(CPACK_PACKAGE_VERSION "1.0")
set(CPACK_PACKAGE_CONTACT "WorkWithValay@Gmail.com")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(UNIX)
    set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")

set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

include(CPack)