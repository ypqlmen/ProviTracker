cmake_minimum_required(VERSION 3.16)
project(ProvisionTrackerV2 VERSION 1.6.1 LANGUAGES CXX)

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

# Empty until the real store listing is published. Never ship a guessed URL.
set(PROVI_CHROME_STORE_ID "" CACHE STRING "Published Chrome Web Store extension ID")
if(PROVI_CHROME_STORE_ID)
    string(LENGTH "${PROVI_CHROME_STORE_ID}" CHROME_ID_LENGTH)
    if(NOT CHROME_ID_LENGTH EQUAL 32 OR NOT PROVI_CHROME_STORE_ID MATCHES "^[a-p]+$")
        message(FATAL_ERROR "Invalid Chrome Web Store extension ID")
    endif()
    target_compile_definitions(ProvisionTrackerV2 PRIVATE PROVI_CHROME_STORE_ID="${PROVI_CHROME_STORE_ID}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(SalesRegistrationTests tests/sales_registration_test.cpp)
    target_link_libraries(SalesRegistrationTests PRIVATE Qt6::Core)
    add_test(NAME SalesRegistrationTests COMMAND SalesRegistrationTests)
    add_executable(PayrollForecastTests tests/payroll_forecast_test.cpp)
    target_link_libraries(PayrollForecastTests PRIVATE Qt6::Core)
    add_test(NAME PayrollForecastTests COMMAND PayrollForecastTests)
    add_executable(IntramanagerCacheTests
        tests/intramanager_cache_test.cpp
        domain.h
    )
    target_link_libraries(IntramanagerCacheTests PRIVATE Qt6::Core)
    add_test(NAME IntramanagerCacheTests COMMAND IntramanagerCacheTests)
endif()
