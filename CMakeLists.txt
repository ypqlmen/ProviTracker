cmake_minimum_required(VERSION 3.16)
project(ProvisionTrackerV2 VERSION 1.5.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core PrintSupport Network)

add_executable(ProvisionTrackerV2 WIN32
    main_v32.cpp
    storage_paths.h
    domain.h
    credentials.h
    repository.h
    commission.h
    report_service.h
)

target_link_libraries(ProvisionTrackerV2 PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::PrintSupport
    Qt6::Network
    crypt32
    advapi32
    bcrypt
)
