# Building libcpu on Windows (Win32)

This guide explains how to build libcpu on Windows using MSVC, MinGW, or Clang.

## Table of Contents

- [Prerequisites](#prerequisites)
- [LLVM Installation](#llvm-installation)
- [Build with CMake and MSVC](#build-with-cmake-and-msvc)
- [Build with MinGW](#build-with-mingw)
- [Using vcpkg for Dependencies](#using-vcpkg-for-dependencies)
- [Troubleshooting](#troubleshooting)
- [Cross-Compilation](#cross-compilation)

## Prerequisites

### Required Tools

1. **CMake** (3.5 or later)
   - Download from: https://cmake.org/download/
   - Make sure to add CMake to your PATH during installation

2. **C++ Compiler** (choose one):
   - **Visual Studio 2017 or later** (recommended)
     - Download from: https://visualstudio.microsoft.com/downloads/
     - Install "Desktop development with C++" workload
   - **MinGW-w64**
     - Download from: https://mingw-w64.org/
   - **Clang for Windows**
     - Download from: https://releases.llvm.org/

3. **Git** (optional, for cloning the repository)
   - Download from: https://git-scm.com/download/win

4. **Python** (2.7 or 3.x)
   - Download from: https://www.python.org/downloads/windows/
   - Required for running build scripts

5. **Flex and Bison** (for UPCL compiler)
   - **Option 1**: Install via winflexbison
     ```powershell
     # Using Chocolatey
     choco install winflexbison
     ```
   - **Option 2**: Download from GnuWin32
     - Flex: http://gnuwin32.sourceforge.net/packages/flex.htm
     - Bison: http://gnuwin32.sourceforge.net/packages/bison.htm

## LLVM Installation

libcpu requires LLVM for JIT compilation. Here are several ways to install LLVM on Windows:

### Method 1: Official Pre-built Binaries (Recommended)

1. Download LLVM installer from:
   https://github.com/llvm/llvm-project/releases

2. Choose the latest stable version (LLVM 10.0+recommended)
   - For 64-bit: `LLVM-<version>-win64.exe`
   - For 32-bit: `LLVM-<version>-win32.exe`

3. Run the installer and install to the default location:
   ```
   C:\Program Files\LLVM
   ```

4. Add LLVM to your PATH:
   - During installation, select "Add LLVM to the system PATH"
   - Or manually add `C:\Program Files\LLVM\bin` to PATH

5. Set environment variable (optional, but recommended):
   ```powershell
   setx LLVM_DIR "C:\Program Files\LLVM"
   ```

### Method 2: Using vcpkg

[vcpkg](https://vcpkg.io/) is a package manager for C++ that simplifies dependency management:

1. Install vcpkg:
   ```powershell
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. Install LLVM:
   ```powershell
   # For 64-bit
   .\vcpkg install llvm:x64-windows

   # For 32-bit
   .\vcpkg install llvm:x86-windows
   ```

3. Integrate with Visual Studio:
   ```powershell
   .\vcpkg integrate install
   ```

4. When building libcpu, use the vcpkg toolchain:
   ```powershell
   cmake -B build -DCMAKE_TOOLCHAIN_FILE="[vcpkg root]/scripts/buildsystems/vcpkg.cmake"
   ```

### Method 3: Build LLVM from Source

Only recommended if you need a specific configuration:

```powershell
# Clone LLVM
git clone --depth=1 --branch release/10.x https://github.com/llvm/llvm-project.git
cd llvm-project

# Configure
cmake -S llvm -B build -G "Visual Studio 16 2019" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DLLVM_ENABLE_PROJECTS="clang" `
    -DLLVM_TARGETS_TO_BUILD="X86" `
    -DLLVM_INCLUDE_EXAMPLES=OFF `
    -DLLVM_INCLUDE_TESTS=OFF

# Build (takes 30-60 minutes)
cmake --build build --config Release -j8

# Install
cmake --install build --prefix C:\LLVM
```

## Build with CMake and MSVC

### Option 1: Using CMake GUI

1. Open CMake GUI
2. Set "Where is the source code" to the libcpu directory
3. Set "Where to build the binaries" to `libcpu/build`
4. Click "Configure"
5. Select your Visual Studio version and platform (x64 recommended)
6. If LLVM is not found automatically, set `LLVM_DIR`:
   - Add Entry → Name: `LLVM_DIR`, Type: `PATH`, Value: `C:/Program Files/LLVM`
7. Click "Generate"
8. Click "Open Project" to open in Visual Studio
9. Build the solution (F7 or Build → Build Solution)

### Option 2: Using Command Line

```powershell
# Open "Developer Command Prompt for VS 2019" (or your VS version)

# Navigate to libcpu directory
cd path\to\libcpu

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake .. -G "Visual Studio 16 2019" -A x64

# Or specify LLVM location explicitly
cmake .. -G "Visual Studio 16 2019" -A x64 -DLLVM_DIR="C:/Program Files/LLVM"

# Build
cmake --build . --config Release

# Or build specific target
cmake --build . --config Release --target cpu
cmake --build . --config Release --target upcc
```

### Build Configurations

CMake supports multiple build configurations:

- **Debug**: Full debugging symbols, no optimization
  ```powershell
  cmake --build . --config Debug
  ```

- **Release**: Optimized, minimal debug info
  ```powershell
  cmake --build . --config Release
  ```

- **RelWithDebInfo**: Optimized with debug symbols
  ```powershell
  cmake --build . --config RelWithDebInfo
  ```

## Build with MinGW

If using MinGW-w64 instead of MSVC:

```powershell
# Ensure MinGW is in PATH
set PATH=C:\mingw-w64\bin;%PATH%

# Configure
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build .
```

## Using vcpkg for Dependencies

vcpkg simplifies dependency management:

```powershell
# Install vcpkg (one-time setup)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# Install LLVM and other dependencies
.\vcpkg install llvm:x64-windows

# Build libcpu with vcpkg
cd path\to\libcpu
mkdir build
cd build

cmake .. -DCMAKE_TOOLCHAIN_FILE="[path-to-vcpkg]\scripts\buildsystems\vcpkg.cmake"
cmake --build . --config Release
```

## Building UPCL Compiler on Windows

The UPCL compiler requires Flex and Bison:

### Install winflexbison

```powershell
# Using Chocolatey
choco install winflexbison

# Or download from:
# https://github.com/lexxmark/winflexbison/releases
```

### Build with UPCL Enabled

```powershell
cmake .. -DBUILD_UPCL=ON
cmake --build . --config Release --target upcc
```

### Test UPCL

```powershell
cd build\upcl\Release
.\upcc.exe ..\..\..\..\upcl\examples\6502.def
```

## Troubleshooting

### LLVM Not Found

**Error**: `Could NOT find LLVM`

**Solutions**:
1. Set `LLVM_DIR` environment variable:
   ```powershell
   setx LLVM_DIR "C:\Program Files\LLVM"
   ```

2. Specify LLVM path explicitly when configuring:
   ```powershell
   cmake .. -DLLVM_DIR="C:/Program Files/LLVM"
   ```

3. Ensure `llvm-config.exe` is in your PATH:
   ```powershell
   set PATH=%PATH%;C:\Program Files\LLVM\bin
   ```

### Linker Errors (LNK2019, LNK2001)

**Error**: Unresolved external symbols for LLVM functions

**Solutions**:
1. Ensure you're building with the same configuration (Debug/Release) as your LLVM installation
2. Check that LLVM libraries are being linked:
   ```cmake
   target_link_libraries(cpu ${LLVM_LIBS_CORE} ${LLVM_LIBS_JIT})
   ```

3. Verify LLVM version compatibility (LLVM 8.0+ recommended)

### Missing Flex/Bison

**Error**: `Could NOT find FLEX` or `Could NOT find BISON`

**Solution**:
```powershell
# Install via Chocolatey
choco install winflexbison

# Or add to PATH if already installed
set PATH=%PATH%;C:\ProgramData\chocolatey\lib\winflexbison\tools
```

### Runtime DLL Not Found

**Error**: `LLVM-C.dll not found` or similar

**Solution**:
1. Add LLVM bin directory to PATH:
   ```powershell
   set PATH=%PATH%;C:\Program Files\LLVM\bin
   ```

2. Or copy LLVM DLLs to your build output directory

### Unicode/Path Issues

**Error**: Build fails with path encoding errors

**Solution**:
```powershell
# Set console to UTF-8
chcp 65001

# Use short paths
cmake .. -DCMAKE_INSTALL_PREFIX=C:/libcpu
```

### Permission Denied Errors

**Error**: `Access is denied` during build

**Solution**:
1. Run Command Prompt or PowerShell as Administrator
2. Disable antivirus temporarily during build
3. Exclude build directory from Windows Defender

## Cross-Compilation

### Building for ARM64 on x64 Windows

```powershell
# Configure for ARM64
cmake .. -G "Visual Studio 16 2019" -A ARM64

# Build
cmake --build . --config Release
```

### Building 32-bit on 64-bit Windows

```powershell
# Configure for Win32
cmake .. -G "Visual Studio 16 2019" -A Win32

# Build
cmake --build . --config Release
```

## Testing the Build

```powershell
# Run tests
cd build
ctest -C Release --output-on-failure

# Or run specific test
cd test\Release
.\test_6502.exe
```

## Creating Distribution Package

```powershell
# Build installer
cd build
cpack -C Release -G NSIS

# Or create ZIP package
cpack -C Release -G ZIP
```

## IDE Integration

### Visual Studio

After running CMake with Visual Studio generator, open `libcpu.sln`:

```powershell
cmake .. -G "Visual Studio 16 2019" -A x64
start libcpu.sln
```

### Visual Studio Code

1. Install CMake Tools extension
2. Open libcpu folder in VS Code
3. Press F1 → "CMake: Configure"
4. Press F7 to build

Configure `.vscode/settings.json`:
```json
{
    "cmake.configureSettings": {
        "LLVM_DIR": "C:/Program Files/LLVM"
    },
    "cmake.buildDirectory": "${workspaceFolder}/build"
}
```

### CLion

1. Open libcpu as CMake project
2. Go to File → Settings → Build, Execution, Deployment → CMake
3. Add CMake option: `-DLLVM_DIR="C:/Program Files/LLVM"`
4. Click "Reload CMake Project"
5. Build with Ctrl+F9

## Performance Considerations

### Optimization Flags

For maximum performance on Windows:

```powershell
cmake .. -DCMAKE_BUILD_TYPE=Release `
         -DCMAKE_CXX_FLAGS="/O2 /Ob2 /GL" `
         -DCMAKE_EXE_LINKER_FLAGS="/LTCG"
```

### Parallel Builds

Speed up compilation:

```powershell
# Use all CPU cores
cmake --build . --config Release -j

# Or specify number of cores
cmake --build . --config Release -j8
```

## Additional Resources

- **libcpu Documentation**: See `README.md` in the root directory
- **UPCL Guide**: See `upcl/README.md` for UPCL language reference
- **LLVM Documentation**: https://llvm.org/docs/
- **CMake Documentation**: https://cmake.org/documentation/
- **vcpkg**: https://vcpkg.io/

## Getting Help

If you encounter issues:

1. Check this document first
2. Review CMake configuration output carefully
3. Ensure LLVM version compatibility (8.0+ recommended)
4. File an issue on GitHub with:
   - Windows version
   - Visual Studio / MinGW version
   - LLVM version
   - CMake version
   - Full error message
   - CMakeCache.txt file

## License

See LICENSE file in the root directory for licensing information.
