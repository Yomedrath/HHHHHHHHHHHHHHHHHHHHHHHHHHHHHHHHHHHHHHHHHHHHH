cmake_minimum_required(VERSION 3.20)
project(proto1v1 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(PROTO_BUILD_GAME "Spiel mit SDL3 bauen" ON)

# --- Kern (ohne Abhängigkeiten) ---
add_library(core STATIC
  src/core/fighter.c
  src/core/stage.c
  src/core/game.c
  src/core/cpu.c
  src/core/replay.c)
target_include_directories(core PUBLIC src/core)
if(NOT MSVC)
  target_link_libraries(core PUBLIC m)
  target_compile_options(core PRIVATE -Wall -Wextra)
endif()

add_executable(sim_test tests/sim_test.c)
target_link_libraries(sim_test PRIVATE core)
enable_testing()
add_test(NAME sim_test COMMAND sim_test)

# --- Spiel ---
if(PROTO_BUILD_GAME)
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    message(STATUS "SDL3 nicht gefunden – wird von GitHub geladen und statisch gebaut")
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.2.26
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
  endif()

  add_executable(proto1v1 WIN32 MACOSX_BUNDLE src/app/main.c)
  target_link_libraries(proto1v1 PRIVATE core SDL3::SDL3)
  set_target_properties(proto1v1 PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "proto1v1")
endif()
