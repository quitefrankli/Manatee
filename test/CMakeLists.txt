cmake_minimum_required(VERSION 3.20)

# set the project name
project(unittests)

file(GLOB_RECURSE TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(unittests ${TEST_SOURCES})
target_include_directories(unittests PRIVATE ${VulkanIncludes})
target_link_libraries(unittests VulkanLibs ${CONAN_LIBS})
add_test(unittests ${CMAKE_BINARY_DIR}/bin/unittests)

# omits the annoying pdb debug symbol missing console spam
if(MSVC)
	set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ignore:4099")
endif()
