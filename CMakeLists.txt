cmake_minimum_required(VERSION 3.15)
project(ClipLite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_options($<$<CONFIG:Release>:/O1>)
    add_link_options($<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(ClipLite WIN32
    src/main.cpp
    src/clip_store.cpp
    src/render_context.cpp
    src/clipboard_monitor.cpp
    src/hotkey_manager.cpp
    src/app_lifecycle.cpp
    src/history_window.cpp
    src/settings_window.cpp
    include/clip_store.h
    include/render_context.h
    include/clipboard_monitor.h
    include/hotkey_manager.h
    include/app_lifecycle.h
    include/history_window.h
    include/settings_window.h
    resources/clipLite.rc
)

target_include_directories(ClipLite PRIVATE include)
target_compile_definitions(ClipLite PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(ClipLite PRIVATE user32 gdi32 shell32 shcore ole32 comctl32 crypt32 dwmapi d2d1 dwrite windowscodecs)

if(MSVC)
    set_target_properties(ClipLite PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ClipLiteStoreTests
        tests/store_tests.cpp
        src/clip_store.cpp
        include/clip_store.h
        resources/clipLite.rc
    )
    target_include_directories(ClipLiteStoreTests PRIVATE include)
    target_compile_definitions(ClipLiteStoreTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(ClipLiteStoreTests PRIVATE shell32 ole32 crypt32)
    add_test(NAME ClipLiteStore COMMAND ClipLiteStoreTests)
endif()
