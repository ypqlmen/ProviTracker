cmake_minimum_required(VERSION 3.16)
project(ProvisionTrackerV2 VERSION 2.0.1 LANGUAGES CXX)

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
    order_product_selection.h
    order_product_picker.h
    orders_view.h
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
    enable_language(RC)
    target_sources(ProvisionTrackerV2 PRIVATE app_icon.rc)
    target_include_directories(ProvisionTrackerV2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ProvisionTrackerV2 PRIVATE crypt32 advapi32 bcrypt)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(SalesMailTests tests/sales_mail_test.cpp)
    target_link_libraries(SalesMailTests PRIVATE Qt6::Core)
    add_test(NAME SalesMailTests COMMAND SalesMailTests)
    add_executable(OrderProductSelectionTests tests/order_product_selection_test.cpp)
    target_link_libraries(OrderProductSelectionTests PRIVATE Qt6::Core)
    add_test(NAME OrderProductSelectionTests COMMAND OrderProductSelectionTests)
    add_executable(SingleSellerAccountTests tests/single_seller_account_test.cpp)
    target_link_libraries(SingleSellerAccountTests PRIVATE Qt6::Core)
    add_test(NAME SingleSellerAccountTests COMMAND SingleSellerAccountTests)
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
