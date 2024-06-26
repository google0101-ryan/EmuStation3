project(PS3)

set(CMAKE_MINIMUM_REQUIRED_VERSION 3.16.3)

set(CXX_STANDARD c++23)

set(SOURCES src/main.cpp
            src/cpu/PPU.cpp
            src/cpu/PPUInstrs.cpp
            src/cpu/SPU.cpp
            src/rsx/rsx.cpp
            src/rsx/VPE.cpp
            src/kernel/Memory.cpp
            src/kernel/ModuleManager.cpp
            src/kernel/Modules/Spinlock.cpp
            src/kernel/Modules/Mutex.cpp
            src/kernel/Modules/CellGcm.cpp
            src/kernel/Modules/CellThread.cpp
            src/kernel/Modules/VFS.cpp
            src/kernel/Modules/CellHeap.cpp
            src/kernel/Modules/CellPad.cpp
            src/kernel/Modules/CellResc.cpp
            src/kernel/Modules/CellSysUtil.cpp
            src/kernel/Modules/CellSpurs.cpp
            src/kernel/Modules/CellGame.cpp
            src/kernel/Syscall.cpp
            src/loaders/Elf.cpp
            src/loaders/SFO.cpp)

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(ps3 ${SOURCES})
set(TARGET_NAME ps3)

find_package(SDL2 REQUIRED)
include_directories(ps3 ${SDL2_INCLUDE_DIRS})

target_link_libraries(ps3 ${SDL2_LIBRARIES})

if(MSVC)
  target_compile_options(${TARGET_NAME} PRIVATE /W4 /WX)
else()
  target_compile_options(${TARGET_NAME} PRIVATE -pg -O3 -g -std=c++2b -msse2 -msse4.1)
  target_link_options(${TARGET_NAME} PRIVATE -pg)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
