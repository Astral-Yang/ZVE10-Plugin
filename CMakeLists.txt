cmake_minimum_required(VERSION 3.16)
project(zve10plugin LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)

option(ZVP_BUILD_HOST_TESTS "构建主机端单元测试" OFF)

# 与宿主单元测试共享的纯 C 逻辑源文件（无相机平台强耦合）
set(ZVP_LOGIC_SOURCES
    src/zvp_gate.c
    src/zvp_wifi5g.c
    src/zvp_recovery.c
    src/zvp_input_hook.c
    src/zvp_sound.c
)

# 相机端完整运行时注入库 libzve10plugin.so（仅在完整构建时编译 LVGL）
if(NOT ZVP_BUILD_HOST_TESTS)
    set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
    set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
    set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
    set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/CMakeLists.txt")
        message(FATAL_ERROR "未找到 third_party/lvgl 源码，请先在仓库根目录运行: git submodule update --init --recursive")
    endif()
    add_subdirectory(third_party/lvgl)

    target_compile_options(lvgl PRIVATE
        -fPIC
        -Os
        -fno-stack-protector
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=0
        -ffunction-sections
        -fdata-sections
    )
    # 编译期构造 GLIBC_2.4 libdl.so.2 符号归属桩，防止新工具链将 dlsym 绑定至 libc.so.6 (GLIBC_2.34)
    add_library(zvp_dl_stub SHARED cmake/dl_stub.c)
    set_target_properties(zvp_dl_stub PROPERTIES
        OUTPUT_NAME "dl"
        SOVERSION "2"
        VERSION "2"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stub"
        NO_SONAME OFF
    )
    target_link_options(zvp_dl_stub PRIVATE
        -nostdlib
        -Wl,--soname,libdl.so.2
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/dl_stub.ver
    )

    add_library(zve10plugin SHARED
        ${ZVP_LOGIC_SOURCES}
        src/zvp_font_zh24.c
        src/zvp_font_zh18.c
        src/zvp_native_bridge.c
        src/zvp_lvgl_port.c
        src/zvp_ui.c
        src/zvp_fopen_hook.c
        src/zvp_entry.c
    )

    target_include_directories(zve10plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(zve10plugin PRIVATE
        lvgl
        zvp_dl_stub
        pthread
    )

    target_compile_options(zve10plugin PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -fPIC
        -Os
        -fno-stack-protector
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=0
        -ffunction-sections
        -fdata-sections
    )

    target_link_options(zve10plugin PRIVATE
        -Wl,--no-as-needed
        -Wl,--gc-sections
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/zve10plugin.ver
    )

    set_target_properties(zve10plugin PROPERTIES
        OUTPUT_NAME "zve10plugin"
        PREFIX "lib"
        SUFFIX ".so"
        SKIP_BUILD_RPATH TRUE
    )
endif()

# 主机端逻辑单元测试可执行文件
if(ZVP_BUILD_HOST_TESTS)
    enable_testing()
    add_executable(test_zvp_logic
        tests/test_zvp_logic.c
        ${ZVP_LOGIC_SOURCES}
    )
    target_include_directories(test_zvp_logic PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(test_zvp_logic PRIVATE ZVP_HOST_TEST=1)
    target_compile_options(test_zvp_logic PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_zvp_logic PRIVATE m)
    add_test(NAME zvp_logic_tests COMMAND test_zvp_logic)
endif()
