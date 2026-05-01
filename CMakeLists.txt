cmake_minimum_required(VERSION 3.10)
project(call_monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find sdbus-c++ via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDBUSCPP REQUIRED sdbus-c++)

# Find PJSIP via pkg-config (required)
pkg_check_modules(PJSIP REQUIRED libpjproject)

# Find nlohmann/json (required for config file)
find_package(nlohmann_json 3.2.0 REQUIRED)

add_executable(call_monitor
    src/main.cpp
    src/CallState.cpp
    src/GsmCallDetector.cpp
    src/SipClient.cpp
)

target_include_directories(call_monitor
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${SDBUSCPP_INCLUDE_DIRS}
        ${PJSIP_INCLUDE_DIRS}
)

target_link_libraries(call_monitor
    PRIVATE
        ${SDBUSCPP_LIBRARIES}
        ${PJSIP_LIBRARIES}
        nlohmann_json::nlohmann_json
        pthread
)

# If you want compile flags from pkg-config too:
target_compile_options(call_monitor
    PRIVATE
        ${SDBUSCPP_CFLAGS_OTHER}
        ${PJSIP_CFLAGS_OTHER}
)
