include("${razer_SOURCE_DIR}/scripts/cmake.global")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

set(GTK_SOURCES
    main.c
    mainwindow.c
    dpipanel.c
    ledpanel.c
    freqpanel.c
    profilepanel.c
    buttonspanel.c
)

set(GTK_HEADERS
    mainwindow.h
    dpipanel.h
    ledpanel.h
    freqpanel.h
    profilepanel.h
    buttonspanel.h
)

add_executable(qrazercfg_gtk ${GTK_SOURCES} ${GTK_HEADERS})

target_compile_options(qrazercfg_gtk PRIVATE
    -std=c2x -Wall -Wextra $<$<CONFIG:Debug>:-g> -rdynamic
    ${GTK4_CFLAGS_OTHER})

target_compile_definitions(qrazercfg_gtk PRIVATE
    LIBRAZERD_NOTIFICATIONS)

target_include_directories(qrazercfg_gtk PRIVATE
    ${GTK4_INCLUDE_DIRS}
    "${razer_SOURCE_DIR}/librazerd"
    "${razer_SOURCE_DIR}/ui/common")

target_link_libraries(qrazercfg_gtk PRIVATE
    ${GTK4_LIBRARIES}
    razerd_client)

set_target_properties(qrazercfg_gtk PROPERTIES OUTPUT_NAME "qrazercfg")

install(TARGETS qrazercfg_gtk DESTINATION bin)
