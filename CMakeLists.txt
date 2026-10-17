cmake_minimum_required(VERSION 3.16)
project(ProvisionTrackerV2 VERSION 1.6.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Network Svg)

add_executable(ProvisionTrackerV2 WIN32
    main_v32.cpp
    settings_icons.qrc
    storage_paths.h
    domain.h
    credentials.h
    repository.h
    commission.h
    report_view.h
    report_service.h
)

target_link_libraries(ProvisionTrackerV2 PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Network
    Qt6::Svg
)

if(WIN32)
    target_link_libraries(ProvisionTrackerV2 PRIVATE crypt32 advapi32 bcrypt)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(FeatureAccessTests tests/feature_access_test.cpp)
    target_link_libraries(FeatureAccessTests PRIVATE Qt6::Core)
    add_test(NAME FeatureAccessTests COMMAND FeatureAccessTests)
    add_executable(SalesRegistrationTests tests/sales_registration_test.cpp)
    target_link_libraries(SalesRegistrationTests PRIVATE Qt6::Core)
    add_test(NAME SalesRegistrationTests COMMAND SalesRegistrationTests)
    add_executable(RetiredIntegrationCleanupTests tests/retired_integration_cleanup_test.cpp)
    target_link_libraries(RetiredIntegrationCleanupTests PRIVATE Qt6::Core)
    add_test(NAME RetiredIntegrationCleanupTests COMMAND RetiredIntegrationCleanupTests)
    add_executable(PayrollForecastTests tests/payroll_forecast_test.cpp)
    target_link_libraries(PayrollForecastTests PRIVATE Qt6::Core)
    add_test(NAME PayrollForecastTests COMMAND PayrollForecastTests)
    add_executable(PayrollAdjustmentsTests tests/payroll_adjustments_test.cpp)
    target_link_libraries(PayrollAdjustmentsTests PRIVATE Qt6::Core)
    add_test(NAME PayrollAdjustmentsTests COMMAND PayrollAdjustmentsTests)
    add_executable(IntramanagerCacheTests
        tests/intramanager_cache_test.cpp
        domain.h
    )
    target_link_libraries(IntramanagerCacheTests PRIVATE Qt6::Core)
    add_test(NAME IntramanagerCacheTests COMMAND IntramanagerCacheTests)
endif()
