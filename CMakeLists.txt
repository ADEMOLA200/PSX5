cmake_minimum_required(VERSION 3.20)
project(psx5 LANGUAGES CXX)

# Optional: compile GLSL to SPIR-V if glslangValidator is available.
find_program(GLSLANG_VALIDATOR glslangValidator)
if(GLSLANG_VALIDATOR)
    message(STATUS "glslangValidator found: ${GLSLANG_VALIDATOR}")
    set(SHADERS
        shaders/quad.vert
        shaders/quad.frag
    )
    foreach(SH IN LISTS SHADERS)
        get_filename_component(SH_NAME ${SH} NAME_WE)
        set(SPIRV ${CMAKE_BINARY_DIR}/shaders/${SH_NAME}.spv)
        add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_SOURCE_DIR}/${SH} -o ${SPIRV}
            DEPENDS ${CMAKE_SOURCE_DIR}/${SH}
            COMMENT "Compiling ${SH} -> ${SPIRV}"
        )
        list(APPEND SPIRV_BINARIES ${SPIRV})
    endforeach()
else()
    message(STATUS "glslangValidator not found; SPIR-V compilation disabled. Provide precompiled .spv in build/shaders/")
endif()

option(ENABLE_VULKAN "Enable Vulkan GPU backend" OFF)
option(ENABLE_ASMJIT "Enable asmjit JIT backend" OFF)
option(ENABLE_GLFW "Enable GLFW windowing and Vulkan swapchain" OFF)
option(ENABLE_SDL2 "Enable SDL2 audio" OFF)
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_QT_GUI "Enable Qt-based GUI" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(ENABLE_QT_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

add_library(psx5_core
    src/runtime/emulator.cpp
    src/core/logger.cpp
    src/core/memory.cpp
    src/core/cpu.cpp
    src/core/jit.cpp
    src/core/jit_asmjit.cpp
    src/core/syscalls.cpp
    src/core/scheduler.cpp
    src/loader/module_loader.cpp
    src/loader/elf64_loader.cpp
    src/gpu/gpu.cpp
    src/gpu/vulkan_glfw.cpp
    src/gpu/vulkan_swapchain.cpp
    src/gpu/vulkan_full.cpp
    src/gpu/spv_embedded.h
    src/audio/audio.cpp
    src/debugger.cpp
)

target_include_directories(psx5_core PUBLIC src)

if(ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_compile_definitions(psx5_core PRIVATE PSX5_ENABLE_VULKAN=1)
    target_link_libraries(psx5_core PRIVATE Vulkan::Vulkan)
endif()

if(ENABLE_GLFW)
    find_package(glfw3 REQUIRED)
    target_compile_definitions(psx5_core PRIVATE PSX5_ENABLE_GLFW=1)
    target_link_libraries(psx5_core PRIVATE glfw)
endif()

if(ENABLE_ASMJIT)
    find_package(SDL2 QUIET)
    if(ENABLE_SDL2 AND NOT SDL2_FOUND)
        message(FATAL_ERROR "SDL2 requested but not found")
    endif()
    # Expect asmjit to be findable as a package; if not, you can add asmjit as a submodule or adjust paths.
    find_package(asmjit REQUIRED)
    target_compile_definitions(psx5_core PRIVATE PSX5_ENABLE_ASMJIT=1)
    target_link_libraries(psx5_core PRIVATE asmjit::asmjit)
endif()

if(ENABLE_QT_GUI)
    add_library(psx5_gui
        src/gui/main_window.cpp
        src/gui/main_window.h
        src/gui/game_list_model.cpp
        src/gui/game_list_model.h
        src/gui/settings_dialog.cpp
        src/gui/settings_dialog.h
        src/gui/psn_manager.cpp
        src/gui/psn_manager.h
        src/gui/trophy_window.cpp
        src/gui/trophy_window.h
        src/gui/log_widget.cpp
        src/gui/log_widget.h
    )
    target_include_directories(psx5_gui PUBLIC src)
    target_link_libraries(psx5_gui PUBLIC psx5_core Qt6::Core Qt6::Widgets)
    target_compile_definitions(psx5_gui PRIVATE PSX5_ENABLE_QT_GUI=1)
endif()

add_executable(psx5 src/main.cpp)
if(ENABLE_QT_GUI)
    target_link_libraries(psx5 PRIVATE psx5_core psx5_gui Qt6::Core Qt6::Widgets)
    target_compile_definitions(psx5 PRIVATE PSX5_ENABLE_QT_GUI=1)
else()
    target_link_libraries(psx5 PRIVATE psx5_core)
endif()

if(BUILD_TESTS)
    add_executable(psx5_tests tests/test_vm.cpp)
    target_link_libraries(psx5_tests PRIVATE psx5_core)
    add_custom_target(check COMMAND psx5_tests DEPENDS psx5_tests)
endif()
