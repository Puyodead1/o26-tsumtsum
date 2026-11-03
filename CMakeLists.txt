cmake_minimum_required(VERSION 3.20)
project(o26-tsumtsum LANGUAGES C)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "o26-tsumtsum builds with MSVC for x64 only (the game is 64-bit)")
endif()

# Static CRT, so the DLL and tools need no Visual C++ redistributable on the cabinet / target PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The I/O library the game loads from modules\ (replaces the cabinet's libacioEx.dll).
add_library(libacioEx SHARED src/acioemu/libacioex_emu.c)
set_target_properties(libacioEx PROPERTIES OUTPUT_NAME libacioEx PREFIX "")
target_include_directories(libacioEx PRIVATE src/common)
target_link_libraries(libacioEx PRIVATE user32 gdi32 advapi32 winscard)

# NFC tag manager, command line.
add_executable(o26tag src/o26tag/o26tag.c)
target_include_directories(o26tag PRIVATE src/common)
target_link_libraries(o26tag PRIVATE winscard)

# NFC tag manager, window (comctl32 v6 manifest via #pragma in the source).
add_executable(o26tag_gui WIN32 src/o26tag/o26tag_gui.c)
target_include_directories(o26tag_gui PRIVATE src/common)
target_link_libraries(o26tag_gui PRIVATE user32 gdi32 comctl32 winscard)
