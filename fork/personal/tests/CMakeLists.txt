cmake_minimum_required(VERSION 3.25)
project(seegram_personal_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(root "../../.." ABSOLUTE)
add_library(personal_modules
    "${root}/Telegram/SourceFiles/fork/personal/module_manager.cpp"
    "${root}/Telegram/SourceFiles/fork/personal/module_preferences.cpp"
    "${root}/Telegram/SourceFiles/fork/personal/module_process.cpp")
target_sources(personal_modules PRIVATE "${root}/Telegram/SourceFiles/fork/personal/release_policy.cpp")
target_include_directories(personal_modules PUBLIC "${root}/Telegram/SourceFiles")
target_compile_options(personal_modules PRIVATE -Wall -Wextra -Werror)
add_executable(module_fixture module_fixture.cpp)
add_executable(SeeGramModuleRunner "${root}/Telegram/SourceFiles/fork/personal/module_runner.cpp")
add_executable(module_manager_tests module_manager_tests.cpp)
target_link_libraries(module_manager_tests PRIVATE personal_modules)
target_compile_options(module_manager_tests PRIVATE -Wall -Wextra -Werror)
add_test(NAME module_failure_isolation COMMAND module_manager_tests $<TARGET_FILE:module_fixture>)
if (APPLE)
    add_test(NAME worker_does_not_inherit_host_descriptors COMMAND module_manager_tests
        $<TARGET_FILE:module_fixture> descriptor-isolation)
endif()
add_test(NAME module_self_check_timeout COMMAND module_manager_tests $<TARGET_FILE:module_fixture> timeout)
add_test(NAME incompatible_module_is_not_started COMMAND module_manager_tests $<TARGET_FILE:module_fixture> incompatible)
add_test(NAME module_preferences_survive_restart COMMAND module_manager_tests $<TARGET_FILE:module_fixture> restore)
add_test(NAME running_module_hang_is_isolated COMMAND module_manager_tests $<TARGET_FILE:module_fixture> heartbeat)
add_test(NAME diagnostic_report_is_sanitized COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/diagnostic_report_test.py"
    $<TARGET_FILE:module_manager_tests> $<TARGET_FILE:module_fixture>)
add_test(NAME built_in_diagnostic_module COMMAND module_manager_tests
    "${CMAKE_CURRENT_BINARY_DIR}/SeeGramModuleRunner" production)
add_executable(release_policy_tests release_policy_tests.cpp)
target_link_libraries(release_policy_tests PRIVATE personal_modules)
add_test(NAME manual_update_release_comparison COMMAND release_policy_tests)
add_test(NAME diagnostic_history_survives_restart COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/journal_test.py"
    $<TARGET_FILE:module_manager_tests> $<TARGET_FILE:module_fixture>)
add_test(NAME source_update_preserves_personal_changes COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/update_candidate_test.py")
