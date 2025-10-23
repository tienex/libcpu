# FindLLVM_Improved.cmake
#
# Improved LLVM detection for cross-platform builds including Win32
#
# This module defines:
#  LLVM_FOUND - System has LLVM
#  LLVM_INCLUDE_DIR - LLVM include directories
#  LLVM_LIB_DIR - LLVM library directory
#  LLVM_BIN_DIR - LLVM binary directory
#  LLVM_COMPILE_FLAGS - Compile flags for LLVM
#  LLVM_LDFLAGS - Linker flags for LLVM
#  LLVM_LIBS_CORE - Core LLVM libraries
#  LLVM_LIBS_JIT - JIT compilation libraries
#  LLVM_VERSION - LLVM version string

cmake_minimum_required(VERSION 3.5)

# Helper function to transform version string to number
function(LLVM_VERSION_TO_NUMBER numerical_result version)
    string(REGEX REPLACE "^([0-9.]+).*$" "\\1" internal_version ${version})
    string(REGEX REPLACE "^([0-9]*).+$" "\\1" major ${internal_version})
    string(REGEX REPLACE "^[0-9]*\\.([0-9]*).*$" "\\1" minor ${internal_version})

    if(NOT minor MATCHES "[0-9]+")
        set(minor 0)
    endif()

    if(NOT major MATCHES "[0-9]+")
        set(major 0)
    endif()

    math(EXPR internal_numerical_result "${major}*1000000 + ${minor}*1000")
    set(${numerical_result} ${internal_numerical_result} PARENT_SCOPE)
endfunction()

# On Windows/MSVC, try multiple detection methods
if(WIN32 OR MSVC)
    message(STATUS "Detecting LLVM on Windows...")

    # Method 1: Check environment variable
    if(DEFINED ENV{LLVM_DIR})
        set(LLVM_ROOT $ENV{LLVM_DIR})
        message(STATUS "Using LLVM from environment: ${LLVM_ROOT}")

    # Method 2: Check vcpkg
    elseif(DEFINED ENV{VCPKG_ROOT})
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(VCPKG_TARGET "x64-windows")
        else()
            set(VCPKG_TARGET "x86-windows")
        endif()

        set(LLVM_ROOT "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET}")
        if(IS_DIRECTORY "${LLVM_ROOT}/include/llvm")
            message(STATUS "Using LLVM from vcpkg: ${LLVM_ROOT}")
        else()
            unset(LLVM_ROOT)
        endif()

    # Method 3: Check common installation paths
    else()
        set(LLVM_SEARCH_PATHS
            "C:/Program Files/LLVM"
            "C:/Program Files (x86)/LLVM"
            "C:/LLVM"
            "C:/llvm"
            "$ENV{ProgramFiles}/LLVM"
            "$ENV{ProgramFiles(x86)}/LLVM"
        )

        foreach(SEARCH_PATH ${LLVM_SEARCH_PATHS})
            if(IS_DIRECTORY "${SEARCH_PATH}")
                set(LLVM_ROOT "${SEARCH_PATH}")
                message(STATUS "Found LLVM at: ${LLVM_ROOT}")
                break()
            endif()
        endforeach()
    endif()

    # Verify LLVM installation on Windows
    if(DEFINED LLVM_ROOT AND IS_DIRECTORY ${LLVM_ROOT})
        set(LLVM_BIN_DIR "${LLVM_ROOT}/bin")
        set(LLVM_LIB_DIR "${LLVM_ROOT}/lib")
        set(LLVM_INCLUDE_DIR "${LLVM_ROOT}/include")

        # Check if include directory exists
        if(NOT IS_DIRECTORY "${LLVM_INCLUDE_DIR}/llvm")
            message(FATAL_ERROR "LLVM headers not found at ${LLVM_INCLUDE_DIR}")
        endif()

        # Try to find llvm-config.exe
        find_program(LLVM_CONFIG_EXECUTABLE
            NAMES llvm-config.exe llvm-config
            PATHS "${LLVM_BIN_DIR}"
            NO_DEFAULT_PATH
        )

        if(LLVM_CONFIG_EXECUTABLE)
            message(STATUS "Found llvm-config: ${LLVM_CONFIG_EXECUTABLE}")

            # Get version from llvm-config
            execute_process(
                COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
                OUTPUT_VARIABLE LLVM_STRING_VERSION
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            # Get compile flags
            execute_process(
                COMMAND ${LLVM_CONFIG_EXECUTABLE} --cxxflags
                OUTPUT_VARIABLE LLVM_COMPILE_FLAGS
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            # Get linker flags
            execute_process(
                COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags
                OUTPUT_VARIABLE LLVM_LDFLAGS
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            # Get libraries
            execute_process(
                COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs core executionengine mcjit native
                OUTPUT_VARIABLE LLVM_LIBS_CORE
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            set(LLVM_LIBS_JIT ${LLVM_LIBS_CORE})

        else()
            # Fallback: Manual library specification for Windows
            message(STATUS "llvm-config not found, using manual Win32 library list")

            set(LLVM_STRING_VERSION "10.0.0")
            set(LLVM_COMPILE_FLAGS "/DLLVM_ON_WIN32")
            set(LLVM_LDFLAGS "")

            # Modern LLVM library names (LLVM 8.0+)
            set(LLVM_LIBS_CORE
                LLVMCore
                LLVMSupport
                LLVMAnalysis
                LLVMBitReader
                LLVMBitWriter
                LLVMTransformUtils
                LLVMScalarOpts
                LLVMInstCombine
                LLVMTarget
                LLVMMC
                LLVMMCParser
            )

            set(LLVM_LIBS_JIT
                LLVMExecutionEngine
                LLVMMCJIT
                LLVMRuntimeDyld
                LLVMCodeGen
                LLVMAsmPrinter
                LLVMSelectionDAG
                ${LLVM_LIBS_CORE}
            )

            # Add target-specific libraries (usually X86 on Windows)
            list(APPEND LLVM_LIBS_JIT
                LLVMX86CodeGen
                LLVMX86AsmParser
                LLVMX86Desc
                LLVMX86Info
                LLVMX86Disassembler
            )
        endif()

        set(LLVM_LIBS_JIT_OBJECTS "")
        set(LLVM_FOUND TRUE)

    else()
        message(FATAL_ERROR
            "Could NOT find LLVM on Windows.\n"
            "Please set LLVM_DIR environment variable or install LLVM to a standard location.\n"
            "Download LLVM from: https://github.com/llvm/llvm-project/releases\n"
            "Or install via vcpkg: vcpkg install llvm")
    endif()

# Unix/Linux/macOS: Use llvm-config
else()
    message(STATUS "Detecting LLVM on Unix/Linux...")

    # Find llvm-config
    find_program(LLVM_CONFIG_EXECUTABLE
        NAMES
            llvm-config-18 llvm-config-17 llvm-config-16 llvm-config-15
            llvm-config-14 llvm-config-13 llvm-config-12 llvm-config-11
            llvm-config-10 llvm-config-9 llvm-config-8 llvm-config-7
            llvm-config
        PATHS
            /usr/local/bin
            /usr/bin
            /opt/local/bin
    )

    if(NOT LLVM_CONFIG_EXECUTABLE)
        message(FATAL_ERROR "Could NOT find llvm-config")
    endif()

    message(STATUS "Found llvm-config: ${LLVM_CONFIG_EXECUTABLE}")

    # Get LLVM version
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
        OUTPUT_VARIABLE LLVM_STRING_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "LLVM version: ${LLVM_STRING_VERSION}")

    # Get directories
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --bindir
        OUTPUT_VARIABLE LLVM_BIN_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
        OUTPUT_VARIABLE LLVM_LIB_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
        OUTPUT_VARIABLE LLVM_INCLUDE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Get compile flags
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --cxxflags
        OUTPUT_VARIABLE LLVM_COMPILE_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Get linker flags
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags
        OUTPUT_VARIABLE LLVM_LDFLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Get libraries
    execute_process(
        COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs core executionengine mcjit native
        OUTPUT_VARIABLE LLVM_LIBS_CORE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(LLVM_LIBS_JIT ${LLVM_LIBS_CORE})
    set(LLVM_LIBS_JIT_OBJECTS "")

    if(LLVM_INCLUDE_DIR)
        set(LLVM_FOUND TRUE)
    endif()
endif()

# Transform version to numerical value
if(LLVM_STRING_VERSION)
    LLVM_VERSION_TO_NUMBER(LLVM_VERSION ${LLVM_STRING_VERSION})
    set(LLVM_VERSION_STRING ${LLVM_STRING_VERSION})
endif()

# Verify LLVM was found
if(LLVM_FOUND)
    message(STATUS "LLVM Found!")
    message(STATUS "  Version: ${LLVM_STRING_VERSION}")
    message(STATUS "  Include dir: ${LLVM_INCLUDE_DIR}")
    message(STATUS "  Lib dir: ${LLVM_LIB_DIR}")
    message(STATUS "  Bin dir: ${LLVM_BIN_DIR}")
else()
    if(LLVM_FIND_REQUIRED)
        message(FATAL_ERROR "Could NOT find LLVM")
    endif()
endif()

# Set standard CMake variables
set(LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIR})
set(LLVM_LIBRARY_DIRS ${LLVM_LIB_DIR})
