cmake_minimum_required(VERSION 3.15)   # 建议使用 3.15 或更高版本
project(muduo VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准（例如 C++17）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 为 clangd 生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 可选：使用默认的构建类型（Debug/Release）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 添加子目录（按依赖顺序，库先于可执行文件）
add_subdirectory(src)      # 构建 libmylib
add_subdirectory(app)      # 构建可执行程序，依赖于 libmylib

# 如果有测试，可以添加（但测试通常通过 enable_testing() 单独控制）
#option(BUILD_TESTS "Build unit tests" ON)
#if(BUILD_TESTS)
    #enable_testing()
    #    add_subdirectory(tests)
    #endif()
