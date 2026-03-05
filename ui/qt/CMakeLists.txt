include("${razer_SOURCE_DIR}/scripts/cmake.global")

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Concurrent)
qt_standard_project_setup()

set(QT_SOURCES
    main.cpp
    mainwindow.cpp
    dpitab.cpp
    ledtab.cpp
    freqtab.cpp
    profiletab.cpp
    buttonstab.cpp
)

set(QT_HEADERS
    mainwindow.h
    dpitab.h
    ledtab.h
    freqtab.h
    profiletab.h
    buttonstab.h
)

add_executable(qrazercfg_qt ${QT_SOURCES} ${QT_HEADERS})

target_compile_options(qrazercfg_qt PRIVATE
    -std=c++17 -Wall -Wextra $<$<CONFIG:Debug>:-g> -rdynamic)

target_include_directories(qrazercfg_qt PRIVATE
    "${razer_SOURCE_DIR}/librazerd"
    "${razer_SOURCE_DIR}/ui/common")

target_compile_definitions(qrazercfg_qt PRIVATE LIBRAZERD_NOTIFICATIONS)

target_link_libraries(qrazercfg_qt PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Concurrent
    razerd_client)

set_target_properties(qrazercfg_qt PROPERTIES OUTPUT_NAME "qrazercfg")

install(TARGETS qrazercfg_qt DESTINATION bin)
