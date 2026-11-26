# CMake Build Configuration for root of project
cmake_minimum_required(VERSION 3.18)
project(cube_solver VERSION 1.0 DESCRIPTION "Rubik's Cube Solver" LANGUAGES C)

# Emit compile_commands.json so clangd (and other tooling) knows the real
# per-file include paths, instead of guessing.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler options (inherited by sub-folders)
set(CMAKE_C_STANDARD 23)
add_compile_options(-Wall -Werror -Wpedantic -Wextra)
add_compile_options(-fdiagnostics-color)

# Enable address sanitizer
# (Comment this out for release builds)
add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)

# Enable PThread library for linking
add_compile_options(-pthread)
add_link_options(-pthread)

# What folders to build
add_subdirectory(hal)
add_subdirectory(app)
