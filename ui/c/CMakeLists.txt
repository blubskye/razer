include("${razer_SOURCE_DIR}/scripts/cmake.global")

add_executable(razercfg_c razercfg.c)

target_compile_options(razercfg_c PRIVATE
    -std=c2x -Wall -Wextra -Wpedantic -g -rdynamic)

target_include_directories(razercfg_c PRIVATE
    "${razer_SOURCE_DIR}/librazerd"
    "${razer_SOURCE_DIR}/ui/common")

target_link_libraries(razercfg_c PRIVATE razerd_client)

set_target_properties(razercfg_c PROPERTIES OUTPUT_NAME "razercfg")

install(TARGETS razercfg_c DESTINATION bin)
