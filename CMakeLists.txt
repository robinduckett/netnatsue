# NetNatsue - a recreation of the lost Babel client library for
# the Creatures Evolution Engine's networking module.
#
# Builds a dependency-free static library.  Engine trees can either
# add_subdirectory() this and link "netnatsue", or just compile the
# five .cpp files directly (see INTEGRATING.md).

cmake_minimum_required(VERSION 3.10)

project(netnatsue CXX)

add_library(netnatsue STATIC
	DSNetManager.cpp
	NetMemoryPack.cpp
	NetMemoryUnpack.cpp
	NetNatsueProtocol.cpp
	NetNatsueSocket.cpp
)

target_include_directories(netnatsue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The idle NET: WHAT status embeds the build date.  Regenerated on
# every build; direct (non-CMake) builds of the sources fall back to
# the compiler's __DATE__ instead.
set(netnatsue_build_date_header
	${CMAKE_CURRENT_BINARY_DIR}/generated/NetBuildDate.h)
add_custom_target(netnatsue-build-date
	COMMAND ${CMAKE_COMMAND} -DOUT=${netnatsue_build_date_header}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildDate.cmake
	BYPRODUCTS ${netnatsue_build_date_header})
add_dependencies(netnatsue netnatsue-build-date)
target_include_directories(netnatsue PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(netnatsue PRIVATE NETNATSUE_BUILD_DATE_HEADER)

if(WIN32)
	target_link_libraries(netnatsue PUBLIC ws2_32)
endif()

# The protocol test harness (needs a Natsue server to talk to)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	option(NETNATSUE_BUILD_TESTS "Build the NetNatsue protocol test harness" ON)
else()
	option(NETNATSUE_BUILD_TESTS "Build the NetNatsue protocol test harness" OFF)
endif()

if(NETNATSUE_BUILD_TESTS)
	add_executable(netnatsue-test test/NetNatsueTest.cpp)
	target_link_libraries(netnatsue-test netnatsue)
endif()
