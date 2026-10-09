cmake_minimum_required(VERSION 3.16)
project(iarmbus_l1 LANGUAGES CXX)

include(CTest)
include(GoogleTest)
option(ENABLE_COVERAGE "Instrument code under test for gcov/lcov" OFF)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
find_package(GTest REQUIRED)

add_library(iarmbus_under_test STATIC
    ../../core/libIARM-dbus.c
    ../../core/libIBus-dbus.c
    ../../core/libIBusDaemon-dbus.c)
set_source_files_properties(
    ../../core/libIARM-dbus.c ../../core/libIBus-dbus.c
    ../../core/libIBusDaemon-dbus.c PROPERTIES LANGUAGE CXX)
target_compile_features(iarmbus_under_test PRIVATE cxx_std_14)
target_compile_definitions(iarmbus_under_test PRIVATE _USE_DBUS TP_ENABLED)
target_include_directories(iarmbus_under_test PRIVATE
    ../../core ../../core/include ../../stubs)
target_link_libraries(iarmbus_under_test PUBLIC
    PkgConfig::GLIB PkgConfig::DBUS pthread)
if(ENABLE_COVERAGE)
    target_compile_options(iarmbus_under_test PRIVATE --coverage -O0 -g)
    target_link_options(iarmbus_under_test PUBLIC --coverage)
endif()

add_executable(run_L1Tests test_memory.cpp test_bus.cpp test_daemon.cpp mocks/core_mocks.cpp)
target_compile_features(run_L1Tests PRIVATE cxx_std_14)
target_compile_definitions(run_L1Tests PRIVATE TP_ENABLED)
target_include_directories(run_L1Tests PRIVATE ../../core ../../core/include)
target_link_libraries(run_L1Tests PRIVATE iarmbus_under_test GTest::gtest_main)
target_link_options(run_L1Tests PRIVATE
    -Wl,--wrap=IARM_Init -Wl,--wrap=IARM_Term
    -Wl,--wrap=IARM_Bus_Connect -Wl,--wrap=IARM_Bus_Disconnect
    -Wl,--wrap=IARM_Bus_RegisterEvent -Wl,--wrap=IARM_Bus_RegisterCall)

gtest_discover_tests(run_L1Tests)

# The shipped daemon entry point, built like core/Makefile.am (compiled as C++).
add_executable(iarmbusd_under_test ../../core/IARMDaemonMain-dbus.c)
set_source_files_properties(../../core/IARMDaemonMain-dbus.c PROPERTIES LANGUAGE CXX)
target_include_directories(iarmbusd_under_test PRIVATE ../../core/include)
target_link_libraries(iarmbusd_under_test PRIVATE iarmbus_under_test)
if(ENABLE_COVERAGE)
    target_compile_options(iarmbusd_under_test PRIVATE --coverage -O0 -g)
endif()

# Runs the real libIARM/libIBus DBus paths against a private dbus-daemon and iarmbusd.
add_executable(run_L1DbusTests
    dbus/dbus_test_env.cpp dbus/test_rpc.cpp dbus/test_events.cpp
    dbus/test_daemon_rpc.cpp dbus/test_lifecycle.cpp
    dbus/test_call_handler.cpp dbus/test_failure_paths.cpp)
target_compile_features(run_L1DbusTests PRIVATE cxx_std_14)
target_compile_definitions(run_L1DbusTests PRIVATE TP_ENABLED
    IARMBUSD_PATH="$<TARGET_FILE:iarmbusd_under_test>")
add_dependencies(run_L1DbusTests iarmbusd_under_test)
target_include_directories(run_L1DbusTests PRIVATE ../../core ../../core/include dbus)
target_link_libraries(run_L1DbusTests PRIVATE iarmbus_under_test GTest::gtest_main)

gtest_discover_tests(run_L1DbusTests TEST_PREFIX "dbus." PROPERTIES TIMEOUT 60)