include("${razer_SOURCE_DIR}/scripts/cmake.global")

option(LIBRAZERD_NOTIFICATIONS "Build with async notification thread support" ON)

add_library(razerd_client SHARED librazerd.c)

target_compile_options(razerd_client PRIVATE
    -std=c2x
    -Wall -Wextra -Wpedantic -Wformat -Wformat-security
    -Wno-unused-parameter
    -fstack-protector
    -D_GNU_SOURCE
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(razerd_client PRIVATE
        -g
        -fsanitize=address,undefined
        -fsanitize=float-divide-by-zero
        -fsanitize=float-cast-overflow
    )
    target_link_options(razerd_client PRIVATE
        -fsanitize=address,undefined
    )
endif()

if(LIBRAZERD_NOTIFICATIONS)
    target_compile_definitions(razerd_client PRIVATE LIBRAZERD_NOTIFICATIONS)
    target_link_libraries(razerd_client PRIVATE pthread)
endif()

set_target_properties(razerd_client PROPERTIES
    OUTPUT_NAME "razerd"
    SOVERSION 1)

install(TARGETS razerd_client DESTINATION lib)
install(FILES librazerd.h DESTINATION include)

# Test binary (only built, not installed)
add_executable(test_librazerd test_librazerd.c)
target_compile_options(test_librazerd PRIVATE -std=c2x -Wall -g)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(test_librazerd PRIVATE -fsanitize=address,undefined)
    target_link_options(test_librazerd PRIVATE -fsanitize=address,undefined)
endif()
target_link_libraries(test_librazerd PRIVATE razerd_client)
if(LIBRAZERD_NOTIFICATIONS)
    target_compile_definitions(test_librazerd PRIVATE LIBRAZERD_NOTIFICATIONS)
endif()
