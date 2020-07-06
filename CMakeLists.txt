cmake_minimum_required (VERSION 3.15)
project(learn_vulkan VERSION 1.0.0 LANGUAGES CXX)
find_package(Vulkan REQUIRED)

add_executable(${PROJECT_NAME} src/main.cpp)
target_include_directories(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (MSVC)
	set (TARGET_BUILD_PLATFORM win32)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
	target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX STRICT _UNICODE UNICODE VK_USE_PLATFORM_WIN32_KHR VK_NO_PROTOTYPES)
else()
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -Wnon-virtual-dtor -Wshadow)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)

add_subdirectory(src/core)
add_subdirectory(src/os)

# creating shaders
set (ShaderOutputDirectory ${PROJECT_BINARY_DIR}/$<CONFIG>/shaders)
add_custom_command(TARGET learn_vulkan PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
	${ShaderOutputDirectory}
)

file(GLOB VULKAN_SHADERS ${PROJECT_SOURCE_DIR}/src/shaders/*.spv)
foreach(CurrentShaderFile IN LISTS VULKAN_SHADERS)
	add_custom_command(TARGET learn_vulkan PRE_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
		${CurrentShaderFile}
		${ShaderOutputDirectory})
endforeach()


