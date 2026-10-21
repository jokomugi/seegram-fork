if (APPLE AND NOT DESKTOP_APP_MAC_STORE)
    if (NOT DESKTOP_APP_DISABLE_AUTOUPDATE)
        message(FATAL_ERROR "SeeGram Personal currently uses local builds. Set DESKTOP_APP_DISABLE_AUTOUPDATE=ON; the personal automatic updater is not configured yet.")
    endif()
    set(output_name "SeeGram Personal")
    set(bundle_identifier "app.jokomugi.seegram.personal")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/fork/personal/upstream.json")
    file(READ "${CMAKE_SOURCE_DIR}/fork/personal/upstream.json" personal_upstream)
    string(JSON personal_release GET "${personal_upstream}" release)
    target_compile_definitions(Telegram PRIVATE PERSONAL_SEEGRAM_RELEASE="${personal_release}")
    find_package(SQLite3 REQUIRED)
    target_link_libraries(Telegram PRIVATE ${SQLite3_LIBRARIES})
    target_sources(Telegram PRIVATE
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/module_manager.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/module_preferences.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/module_process.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/release_checker.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/release_policy.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/module_ui.cpp)
    target_sources(Telegram PRIVATE
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/session_intro.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/session_account.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/session_source.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/session_import.cpp
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/session_picker_mac.mm)
    add_executable(SeeGramModuleRunner
        ${CMAKE_SOURCE_DIR}/Telegram/SourceFiles/fork/personal/module_runner.cpp)
    set_target_properties(SeeGramModuleRunner PROPERTIES CXX_STANDARD 20)
    add_dependencies(Telegram SeeGramModuleRunner)
    add_custom_command(TARGET Telegram POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Telegram>/../Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:SeeGramModuleRunner>"
            "$<TARGET_FILE_DIR:Telegram>/../Helpers/SeeGramModuleRunner")
endif()
