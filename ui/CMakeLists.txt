include("${razer_SOURCE_DIR}/scripts/cmake.global")

set(UI_BACKEND "python" CACHE STRING "UI backend to build: python | qt | gtk")
set_property(CACHE UI_BACKEND PROPERTY STRINGS python qt gtk)

message(STATUS "UI backend: ${UI_BACKEND}")

if(UI_BACKEND STREQUAL "python")
    add_subdirectory(python)
elseif(UI_BACKEND STREQUAL "qt")
    add_subdirectory(c)
    add_subdirectory(qt)
elseif(UI_BACKEND STREQUAL "gtk")
    add_subdirectory(c)
    add_subdirectory(gtk)
else()
    message(FATAL_ERROR
        "Unknown UI_BACKEND '${UI_BACKEND}'. Valid values: python, qt, gtk")
endif()

# Icons are shared by all backends
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icons/razercfg-icon.svg
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps
    RENAME razercfg.svg)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icons/razercfg-alt.svg
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/icons/razercfg-snake.svg
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps)
