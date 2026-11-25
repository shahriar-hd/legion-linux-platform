cmake_minimum_required(VERSION 3.20)
project(legion_linux_platform VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

add_library(legion_core STATIC
    core/src/posix_io.cpp
    core/src/json.cpp
    core/src/lighting.cpp
)
target_include_directories(legion_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

add_library(legion_backends STATIC
    backends/linux/src/linux_telemetry.cpp
    backends/nvidia/src/nvidia_telemetry.cpp
    backends/lenovo/src/lenovo_telemetry.cpp
    backends/lenovo/src/lenovo_rgb.cpp
)
target_include_directories(legion_backends PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/linux/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/nvidia/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/lenovo/include
)
target_link_libraries(legion_backends PUBLIC legion_core)

add_executable(legion-cli
    apps/cli/main.cpp
)
target_link_libraries(legion-cli PRIVATE legion_backends legion_core)

add_executable(legion-helper
    apps/helper/main.cpp
)
target_link_libraries(legion-helper PRIVATE legion_backends legion_core)

add_executable(legion-lightd
    apps/lightd/main.cpp
)
target_link_libraries(legion-lightd PRIVATE legion_backends legion_core Threads::Threads)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    add_executable(legion-gui
        apps/desktop/main.cpp
    )
    target_link_libraries(legion-gui PRIVATE legion_backends legion_core Qt6::Widgets)
endif()

enable_testing()
add_executable(legion_unit_tests
    tests/unit/test_telemetry.cpp
)
target_link_libraries(legion_unit_tests PRIVATE legion_backends legion_core)
target_compile_options(legion_unit_tests PRIVATE -UNDEBUG)
add_test(NAME legion_unit_tests COMMAND legion_unit_tests)

add_executable(legion_lighting_tests
    tests/unit/test_lighting.cpp
)
target_link_libraries(legion_lighting_tests PRIVATE legion_backends legion_core)
target_compile_options(legion_lighting_tests PRIVATE -UNDEBUG)
add_test(NAME legion_lighting_tests COMMAND legion_lighting_tests)
