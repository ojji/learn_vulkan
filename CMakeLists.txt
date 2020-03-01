cmake_minimum_required (VERSION 3.15)
project(learn_vulkan_prj VERSION 1.0.0 LANGUAGES CXX)
find_package(Vulkan REQUIRED)

add_executable(learn_vulkan src/main.cpp)
target_include_directories(learn_vulkan
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (MSVC)
	target_compile_options(learn_vulkan PRIVATE /W4 /WX)
	target_compile_definitions(learn_vulkan PRIVATE NOMINMAX STRICT UNICODE VK_USE_PLATFORM_WIN32_KHR VK_NO_PROTOTYPES)
else()
	target_compile_options(learn_vulkan PRIVATE -Wall -Wextra -pedantic -Wnon-virtual-dtor -Wshadow)
endif()

target_compile_features(learn_vulkan PUBLIC cxx_std_17)
target_link_libraries(learn_vulkan PRIVATE Vulkan::Vulkan)

add_subdirectory(src/core)
add_subdirectory(src/os)