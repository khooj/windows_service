cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(windows_service)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(generate_product_version)
generate_product_version(
	VERSION_FILE
	NAME "windows_service"
	ICON ${CMAKE_CURRENT_SOURCE_DIR}/windows_service.ico
	VERSION_MAJOR 1
	VERSION_MINOR 1
	VERSION_PATH ${BUILD_COUNTER}
	VERSION_REVISION ${BUILD_REVISION}
)

set(SOURCES
	main.cpp
	service_base.cpp
	service_installer.cpp
	updater_service.cpp)

set(HEADERS
	json.hpp
	service_base.h
	service_installer.h
	updater_service.h)

add_subdirectory(thirdparty)

add_executable(windows_service ${SOURCES} ${VERSION_FILE})
set_target_properties(windows_service 
	PROPERTIES
	VERSION "1.1")
target_link_libraries(windows_service reproc::reproc++ libcurl curl)