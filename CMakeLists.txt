cmake_minimum_required(VERSION 3.16)

project(ratelimit
    VERSION 1.0.0
    DESCRIPTION "A small, dependency-free token-bucket rate limiter in C17"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# ---------------------------------------------------------------- library --
add_library(token_bucket STATIC src/token_bucket.c)
target_include_directories(token_bucket
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# libm is separate on Linux/macOS but part of the C runtime on Windows.
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(token_bucket PUBLIC ${MATH_LIBRARY})
endif()

# -------------------------------------------------------------------- CLI --
add_executable(ratelimit_cli src/main.c)
target_link_libraries(ratelimit_cli PRIVATE token_bucket)
set_target_properties(ratelimit_cli PROPERTIES OUTPUT_NAME ratelimit)

# ------------------------------------------------------------------ tests --
enable_testing()

add_executable(test_token_bucket tests/test_token_bucket.c)
target_link_libraries(test_token_bucket PRIVATE token_bucket)

add_test(NAME token_bucket COMMAND test_token_bucket)
