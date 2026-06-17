cmake_minimum_required(VERSION 3.20)
project(xplat_determinism C)

include(FetchContent)
FetchContent_Declare(
  box2d
  GIT_REPOSITORY https://github.com/erincatto/box2d.git
  GIT_TAG v3.1.1          # latest Box2D v3 stable
)
set(BOX2D_SAMPLES   OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

add_executable(xplat main.c)
target_link_libraries(xplat PRIVATE box2d)

# Determinism: do NOT enable fast-math or FMA contraction. These are the flags
# that silently break cross-platform float reproducibility.
if (MSVC)
  target_compile_options(xplat PRIVATE /fp:precise)
else()
  target_compile_options(xplat PRIVATE -ffp-contract=off -fno-fast-math)
endif()
