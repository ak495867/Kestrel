cmake_minimum_required(VERSION 3.20)
project(Kestrel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/O2 /arch:AVX2 /GL)
    add_link_options(/LTCG)
else()
    add_compile_options(-O3 -march=native -flto -mavx2 -Wall -Wextra)
    add_link_options(-flto)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(kestrel_bench
    src/main.cpp
)

add_executable(kestrel_bypass_test
    src/test_bypass.cpp
)

add_executable(kestrel_ob_test
    src/test_order_book.cpp
)
