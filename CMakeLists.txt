cmake_minimum_required(VERSION 3.14)

project("libSWOC")
include(CMakeDependentOption)
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX ${CMAKE_HOME_DIRECTORY}/image)
endif ()

include(CTest)

# Fortunately this has no external dependencies so the set up can be simple.
add_subdirectory(code)

if (BUILD_TESTING)
    add_subdirectory(unit_tests)
    add_subdirectory(example)
endif()

add_subdirectory(doc EXCLUDE_FROM_ALL)

