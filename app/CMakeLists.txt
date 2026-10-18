# Build the app, using the HAL

include_directories(include)
file(GLOB MY_SOURCES "src/*.c")
add_executable(cube_solver ${MY_SOURCES})

# Make use of the HAL library
target_link_libraries(cube_solver LINK_PRIVATE hal)
