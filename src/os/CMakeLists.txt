set(OS_HEADERS Common.h TypeDefs.h Window.h)
set(OS_SOURCES ${TARGET_BUILD_PLATFORM}/Common.cpp Common.cpp Window.cpp)

target_sources(learn_vulkan PRIVATE ${OS_HEADERS} ${OS_SOURCES})
