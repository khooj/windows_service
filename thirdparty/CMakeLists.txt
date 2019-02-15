set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
add_subdirectory(curl-7.61.1)
add_library(curl INTERFACE)
target_include_directories(curl INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/curl-7.61.1/include")

set(REPROC++ ON CACHE BOOL "" FORCE)
add_subdirectory(reproc-4.0.0)