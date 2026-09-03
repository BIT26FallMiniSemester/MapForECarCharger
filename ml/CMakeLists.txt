cmake_minimum_required(VERSION 3.16)
project(pklot_ml LANGUAGES CXX)

add_executable(pklot_ml src/main.cpp)
target_compile_features(pklot_ml PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(pklot_ml PRIVATE /W4)
else()
    target_compile_options(pklot_ml PRIVATE -Wall -Wextra -Wpedantic)
endif()

