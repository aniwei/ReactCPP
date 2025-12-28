# React C++ 库 CMake 配置
# 
# 这是 ReactJS 到 JSI C++ 转译项目的构建配置
# 
# 目录结构:
# - shared/     共享模块 (ReactFeatureFlags, ReactSymbols, SharedUtils 等)
# - reconciler/ Fiber 协调器模块 (ReactWorkTags, ReactFiberFlags 等)
# - scheduler/  调度器模块 (SchedulerPriorities, SchedulerMinHeap 等)
# - runtime/    运行时抽象 (ReactHostRuntime)

cmake_minimum_required(VERSION 3.16)
project(ReactCPP VERSION 0.1.0 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
        add_compile_definitions(DEV=1)
    else()
        add_compile_options(-O3)
    endif()
elseif(MSVC)
    add_compile_options(/W4)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_definitions(DEV=1)
    endif()
endif()

# =============================================================================
# 查找依赖
# =============================================================================

# JSI 库 (来自 packages/jsi)
set(JSI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../jsi")
if(EXISTS "${JSI_DIR}/jsi/jsi.h")
    message(STATUS "Found JSI at: ${JSI_DIR}")
    
    # 添加 JSI 子目录以构建 JSI 库
    add_subdirectory(${JSI_DIR}/jsi ${CMAKE_BINARY_DIR}/jsi)
else()
    message(WARNING "JSI not found at: ${JSI_DIR}")
endif()

# =============================================================================
# ReactCPP 库
# =============================================================================

# 头文件目录
set(REACT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${JSI_DIR}
)

# 收集源文件
file(GLOB_RECURSE REACT_HEADERS
    "src/shared/*.h"
    "src/reconciler/*.h"
    "src/scheduler/*.h"
    "src/runtime/*.h"
)

# 创建接口库 (header-only)
add_library(ReactCPP INTERFACE)

target_include_directories(ReactCPP INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${JSI_DIR}>
    $<INSTALL_INTERFACE:include>
)

# =============================================================================
# 单元测试
# =============================================================================

option(REACT_BUILD_TESTS "Build ReactCPP unit tests" ON)

if(REACT_BUILD_TESTS)
    # 查找或下载 GoogleTest
    include(FetchContent)
    
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    
    # 防止 GoogleTest 覆盖编译器/链接器设置
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    enable_testing()
    
    # 收集测试源文件
    file(GLOB_RECURSE TEST_SOURCES
        "src/shared/__tests__/*.cpp"
        "src/reconciler/__tests__/*.cpp"
        "src/scheduler/__tests__/*.cpp"
        "src/runtime/__tests__/*.cpp"
        "tests/*.cpp"
    )
    
    if(TEST_SOURCES)
        # 创建测试可执行文件
        add_executable(ReactCPP_tests ${TEST_SOURCES})
        
        target_link_libraries(ReactCPP_tests
            PRIVATE
            ReactCPP
            jsi
            GTest::gtest_main
        )
        
        target_include_directories(ReactCPP_tests PRIVATE
            ${REACT_INCLUDE_DIRS}
        )
        
        # 注册测试
        include(GoogleTest)
        gtest_discover_tests(ReactCPP_tests)
        
        # 添加运行测试的自定义目标
        add_custom_target(run_tests
            COMMAND ReactCPP_tests
            DEPENDS ReactCPP_tests
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running ReactCPP unit tests..."
        )
    else()
        message(STATUS "No test sources found")
    endif()
endif()

# =============================================================================
# 安装配置
# =============================================================================

include(GNUInstallDirs)

install(TARGETS ReactCPP
    EXPORT ReactCPPTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/react
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT ReactCPPTargets
    FILE ReactCPPTargets.cmake
    NAMESPACE React::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReactCPP
)

# =============================================================================
# 打印配置信息
# =============================================================================

message(STATUS "")
message(STATUS "ReactCPP Configuration:")
message(STATUS "  Version:         ${PROJECT_VERSION}")
message(STATUS "  Build Type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:    ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Tests:     ${REACT_BUILD_TESTS}")
message(STATUS "  JSI Directory:   ${JSI_DIR}")
message(STATUS "")
