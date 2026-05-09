cmake_minimum_required(VERSION 3.16)
project(ProvisionTrackerV2 VERSION 1.3.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core PrintSupport)

add_executable(ProvisionTrackerV2 WIN32
    main_v32.cpp
)

target_link_libraries(ProvisionTrackerV2 PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::PrintSupport
    advapi32
)
