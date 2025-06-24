cmake_minimum_required(VERSION 3.10)
project(dControlHook C)

add_compile_options(-m32)
add_link_options(-m32)

add_library(hook SHARED hook.c)
target_link_options(hook PRIVATE -static)
target_link_libraries(hook PRIVATE user32)


add_executable(loader loader.c)
target_link_options(loader PRIVATE -static)


target_link_libraries(loader PRIVATE shell32)