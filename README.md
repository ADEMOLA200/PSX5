# PSX5 - PlayStation 5 Emulator

A high-performance PlayStation 5 emulator implementing full system emulation with RDNA2 GPU architecture, AMD Zen CPU emulation, and PS5-specific hardware components.

## What is PSX5?

PSX5 is a comprehensive PS5 system emulator that recreates the PlayStation 5's hardware architecture in software. It emulates the custom AMD Zen 2 CPU, RDNA2 GPU, Tempest 3D AudioTech, Sony I/O Complex, and the FreeBSD-based PS5 operating system.

## Current Capabilities

### CPU Emulation
- **AMD Zen x86-64 Architecture**: Full 64-bit register set (RAX-R15, XMM0-XMM15, YMM0-YMM15)
- **Advanced JIT Compilation**: Multiple backends (AsmJit, LLVM IR, interpreter) with optimization passes
- **Memory Management**: Virtual memory with paging, TLB caching, and PS5-specific memory layout
- **Privilege Levels**: Hypervisor, kernel, and user mode support
- **SIMD Support**: SSE/AVX instruction sets for high-performance computing

### GPU Emulation (RDNA2)
- **Authentic RDNA2 Architecture**: 4 shader engines with 10 compute units each (matching PS5 hardware)
- **Complete Graphics Pipeline**: Vertex/tessellation/geometry/fragment pipeline with tile-based rendering
- **Advanced Features**:
  - Variable Rate Shading
  - Mesh Shaders
  - Hierarchical Z-buffer with early rejection
  - 64 ROPs for render output
- **16GB GPU Memory**: Full GPU memory allocation and management
- **Vulkan Backend**: Optional hardware acceleration support

### Audio System
- **Tempest 3D AudioTech**: Full spatial audio processing with HRTF
- **Multi-channel Audio**: 8-channel mixing with real-time effects
- **Audio Effects**: Compression, limiting, reverb, and EQ processing
- **DualSense Integration**: Haptic feedback and adaptive trigger support

### I/O and System Features
- **Sony I/O Complex**: Custom PS5 I/O silicon emulation
- **DualSense Controller**: Complete controller state including touchpad, gyroscope, and haptics
- **SSD Controller**: High-speed storage with Kraken compression/decompression
- **Network Stack**: Socket operations and network interface management
- **Security Processor**: Secure boot, encryption/decryption, and access control

### System Software
- **PS5 BIOS**: FreeBSD-based kernel with PS5-specific system calls
- **ELF64 Loader**: Loads PS5 executables and libraries
- **Process Management**: Multi-process support with memory protection
- **File System**: Device mounting and file operations

### Advanced Features
- **PKG File Support**: Complete PS5 package parsing, decryption, and extraction
- **Trophy System**: PlayStation achievement tracking with persistence and unlock logic
- **PlayStation Network Integration**: User authentication, friends, messaging, and cloud saves
- **Advanced Debugging Tools**: Memory analysis, disassembly, profiling, and scripting interface
- **Real-time System Monitoring**: CPU/GPU/memory statistics and performance metrics

## Building PSX5

### Prerequisites

- **CMake 3.20+**
- **C++20 compatible compiler** (GCC 10+, Clang 12+, MSVC 2019+)
- **Required Dependencies**:
  - OpenSSL (for cryptographic operations)
  - zlib (for compression)
  - Capstone (for disassembly)
  - cURL (for network operations)
- **Optional Dependencies**:
  - Vulkan SDK (for GPU acceleration)
  - GLFW3 (for windowing)
  - AsmJit (for JIT compilation)
  - SDL2 (for audio)
  - glslangValidator (for shader compilation)

### Build Options

\`\`\`bash
# Basic build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Full-featured build with all backends
cmake -DENABLE_VULKAN=ON -DENABLE_ASMJIT=ON -DENABLE_GLFW=ON -DENABLE_SDL2=ON ..
make -j$(nproc)

# Development build with tests
cmake -DBUILD_TESTS=ON ..
make -j$(nproc)
make check  # Run tests
\`\`\`

### Build Targets

- `psx5` - Main emulator executable
- `psx5_core` - Core emulation library
- `psx5_tests` - Unit tests (if BUILD_TESTS=ON)

## Running PSX5

### Basic Usage

\`\`\`bash
# Run a PS5 executable
./psx5 <path-to-ps5-executable> [base-address]

# Example
./psx5 game.elf 0x1000

# Run with debugger (default mode)
./psx5 homebrew.bin

# Load PS5 PKG file
./psx5 --pkg game.pkg

# Enable PSN features
./psx5 --psn-login username@email.com game.elf

# Advanced debugging mode
./psx5 --debug --profile game.elf
\`\`\`

### Command Line Options

- `<path-to-executable>` - Path to PS5 ELF64 binary or raw binary
- `[base-address]` - Optional base address for loading (default: 0x1000)
- `--pkg <file>` - Load PS5 package file
- `--psn-login <email>` - Enable PSN integration with user login
- `--debug` - Enable advanced debugging features
- `--profile` - Enable performance profiling
- `--trophy-sync` - Sync trophies with PSN servers

### Supported File Formats

- **ELF64**: PS5 executable format
- **Raw Binary**: Direct memory loading at specified address
- **PS5 PKG**: PlayStation package files with full decryption support

## Current Status

### Implemented
- Complete CPU instruction set emulation
- RDNA2 GPU architecture with compute and graphics pipelines
- Memory management with virtual memory and paging
- Audio processing with 3D spatial audio
- Basic file system operations
- Debugger with REPL interface
- JIT compilation with multiple backends
- **PKG file parsing and decryption**
- **Complete trophy system with persistence**
- **Full PSN integration with authentication**
- **Advanced debugging with profiling and scripting**
- **Real-time system monitoring and statistics**

### In Development
- Game compatibility improvements
- Advanced GPU features optimization
- Network stack completion
- Save state functionality
- Performance profiling tools

### Planned Features
- Enhanced game compatibility
- Save state functionality
- Performance optimizations
- Additional debugging features

## Debugging

PSX5 includes a comprehensive debugging system with advanced analysis tools:

\`\`\`bash
# Basic debugger commands
help          # Show available commands
step          # Execute single instruction
continue      # Continue execution
break <addr>  # Set breakpoint
info regs     # Show CPU registers
info mem      # Show memory layout
disasm        # Disassemble current instruction

# Advanced debugging features
profile start # Start performance profiling
profile stop  # Stop profiling and show results
trace on      # Enable execution tracing
watch <addr>  # Set memory watchpoint
script <file> # Run debugging script
analyze mem   # Perform memory analysis
callstack     # Show call stack with symbols
\`\`\`

### Debugging Scripts

PSX5 supports Lua scripting for automated debugging:

\`\`\`lua
-- Example debugging script
function on_breakpoint(addr, regs)
    print("Hit breakpoint at: " .. string.format("0x%x", addr))
    print("RAX: " .. string.format("0x%x", regs.rax))
end

-- Set conditional breakpoint
set_breakpoint(0x1000, function(regs) 
    return regs.rax == 0x42 
end)
\`\`\`

## Trophy System

PSX5 includes a complete PlayStation trophy system:

\`\`\`bash
# Trophy management
./psx5 --trophy-list game.elf        # List available trophies
./psx5 --trophy-unlock <id> game.elf # Unlock specific trophy
./psx5 --trophy-sync game.elf        # Sync with PSN servers
\`\`\`

## PlayStation Network Integration

Connect to PSN for online features:

\`\`\`bash
# PSN authentication
./psx5 --psn-login user@email.com
# Enter password when prompted

# PSN features available:
# - Friend list synchronization
# - Trophy synchronization
# - Cloud save upload/download
# - Online multiplayer support
\`\`\`

## Configuration

### Memory Layout
- **User Memory**: 4GB-12GB (8GB available)
- **Kernel Memory**: 32GB-36GB (4GB kernel space)
- **GPU Memory**: 36GB-52GB (16GB GPU memory)
- **Hypervisor**: 240GB+ (256MB hypervisor)

### Performance Tuning
- Enable JIT compilation for better performance
- Use Vulkan backend for GPU acceleration
- Adjust memory allocation for your system
- Enable multi-threading for parallel execution

## System Requirements

### Minimum
- **CPU**: x86-64 processor with SSE4.1
- **RAM**: 8GB system memory
- **GPU**: DirectX 11 compatible (for Vulkan backend)
- **Storage**: 2GB free space

### Recommended
- **CPU**: Modern x86-64 with AVX2 support
- **RAM**: 16GB+ system memory
- **GPU**: Vulkan 1.2 compatible GPU with 4GB+ VRAM
- **Storage**: SSD with 10GB+ free space

## Contributing

We welcome contributions! Please see our contributing guidelines for:
- Code style and formatting
- Testing requirements
- Pull request process
- Issue reporting

## 📄 License

PSX5 is released under the MIT License. See LICENSE file for details.

## ⚠️ Legal Notice

This project is for educational and research purposes. PSX5 does not include any copyrighted PlayStation 5 firmware, BIOS, or game content. Users must provide their own legally obtained PS5 system files and software.

## Links

- **Documentation**: [Wiki](https://github.com/ADEMOLA200/psx5/wiki)
- **Issue Tracker**: [GitHub Issues](https://github.com/ADEMOLA200/psx5/issues)
- **Discussions**: [GitHub Discussions](https://github.com/ADEMOLA200/psx5/discussions)

---

**Note**: PSX5 is an active development project. Compatibility and performance may vary. Please report issues and contribute to help improve the emulator!
