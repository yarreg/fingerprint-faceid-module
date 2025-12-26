# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)


# Get short commit hash
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_SHORT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND git rev-list --count HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_COUNT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get build timestamp
execute_process(
  COMMAND date +%Y-%m-%d_%H-%M-%S
  OUTPUT_VARIABLE BUILD_TIMESTAMP
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Set PROJECT_VER to commit count
set(AUTO_SEMVER "1.0.${GIT_COMMIT_COUNT}")
set(PROJECT_VER ${AUTO_SEMVER})

# Add our custom components
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

# Add the git hash and build timestamp as compile definitions
add_compile_definitions(FW_GIT_HASH=\"${GIT_COMMIT_SHORT}\")
add_compile_definitions(BUILD_TIMESTAMP=\"${BUILD_TIMESTAMP}\")

project(fingerprint-faceid-module VERSION ${PROJECT_VER})
