cmake_minimum_required(VERSION 3.20)

project(Tcmalloc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

FetchContent_Declare(
  glog
  GIT_REPOSITORY https://github.com/google/glog.git
  GIT_TAG v0.7.1
)

set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
set(WITH_GTEST OFF CACHE BOOL "" FORCE)
set(TCMALLOC_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(glog)
set(BUILD_TESTING ${TCMALLOC_BUILD_TESTING})

file(GLOB_RECURSE TCMALLOC_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/*.cc"
)

if(TCMALLOC_SOURCES)
  add_library(tcmalloc STATIC ${TCMALLOC_SOURCES})
  target_include_directories(tcmalloc
    PUBLIC
      "${PROJECT_SOURCE_DIR}/include"
  )
  target_link_libraries(tcmalloc PUBLIC glog::glog)
else()
  add_library(tcmalloc INTERFACE)
  target_include_directories(tcmalloc
    INTERFACE
      "${PROJECT_SOURCE_DIR}/include"
  )
  target_link_libraries(tcmalloc INTERFACE glog::glog)
endif()

if(BUILD_TESTING)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  FetchContent_MakeAvailable(googletest)

  include(GoogleTest)

  file(GLOB_RECURSE TCMALLOC_TEST_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/tests/*.cc"
  )

  if(TCMALLOC_TEST_SOURCES)
    add_executable(tests_main ${TCMALLOC_TEST_SOURCES})
    target_link_libraries(tests_main
      PRIVATE
        tcmalloc
        GTest::gtest
    )
    gtest_discover_tests(tests_main)
  endif()
endif()

file(GLOB_RECURSE TCMALLOC_BENCHMARK_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/benchmark/*.cc"
)

foreach(benchmark_source IN LISTS TCMALLOC_BENCHMARK_SOURCES)
  get_filename_component(benchmark_name "${benchmark_source}" NAME_WE)
  add_executable("${benchmark_name}" "${benchmark_source}")
  target_link_libraries("${benchmark_name}"
    PRIVATE
      tcmalloc
      glog::glog
  )
endforeach()
