set(CORE_HEADERS
    Application.h
    CopyToLocalBufferJob.h
    CopyToLocalImageJob.h
    CopyToLocalJob.h
    Mat4.h
    stb_image.h
    Transition.h
    VulkanFunctions.h
    VulkanRenderer.h)

set(CORE_SOURCES
    Application.cpp CopyToLocalBufferJob.cpp CopyToLocalImageJob.cpp
    CopyToLocalJob.cpp Mat4.cpp VulkanRenderer.cpp)

target_sources(${PROJECT_NAME} PRIVATE ${CORE_HEADERS} ${CORE_SOURCES})
