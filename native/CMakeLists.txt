cmake_minimum_required(VERSION 3.20)
project(skyroads C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -O2")

find_package(SDL2 REQUIRED CONFIG)

set(CORE_SRC
    src/compat.c
    src/platform.c
)

add_executable(asset_test ${CORE_SRC} src/main_test.c)
target_include_directories(asset_test PRIVATE src)
target_link_libraries(asset_test PRIVATE SDL2::SDL2)

add_executable(skyroads ${CORE_SRC} src/assets.c src/menus.c src/audio.c src/opl3.c src/render.c src/game_play.c src/main.c)
target_include_directories(skyroads PRIVATE src)
target_link_libraries(skyroads PRIVATE SDL2::SDL2)

add_executable(trek_test src/compat.c src/assets_headless.c src/render.c src/trek_test.c)
target_include_directories(trek_test PRIVATE src)

add_executable(game_sim src/compat.c src/render.c src/game_play.c src/game_sim.c)
target_include_directories(game_sim PRIVATE src)

add_executable(cars_dump src/compat.c src/assets_headless.c src/cars_dump.c)
target_include_directories(cars_dump PRIVATE src)
