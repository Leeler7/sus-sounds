cmake_minimum_required(VERSION 3.20)
project(plinko_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  box2d
  GIT_REPOSITORY https://github.com/erincatto/box2d.git
  GIT_TAG v3.1.1
)
set(BOX2D_SAMPLES    OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

# The deterministic physics core + sound engine (JUCE-free, unit-testable).
add_library(plinko_core STATIC PhysicsCore.cpp SoundEngine.cpp)
target_include_directories(plinko_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(plinko_core PUBLIC box2d)

# Determinism: no fast-math / FMA contraction (cross-platform determinism rule).
if (MSVC)
  target_compile_options(plinko_core PRIVATE /fp:precise)
else()
  target_compile_options(plinko_core PRIVATE -ffp-contract=off -fno-fast-math)
endif()

enable_testing()
add_executable(determinism_test tests/determinism_test.cpp)
target_link_libraries(determinism_test PRIVATE plinko_core)
if (MSVC)
  target_compile_options(determinism_test PRIVATE /fp:precise)
else()
  target_compile_options(determinism_test PRIVATE -ffp-contract=off -fno-fast-math)
endif()
add_test(NAME determinism COMMAND determinism_test)

# Audible demo: physics -> sound engine -> WAV.
add_executable(render_demo tools/render_demo.cpp)
target_link_libraries(render_demo PRIVATE plinko_core)
if (MSVC)
  target_compile_options(render_demo PRIVATE /fp:precise)
else()
  target_compile_options(render_demo PRIVATE -ffp-contract=off -fno-fast-math)
endif()
