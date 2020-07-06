set(UTILS_HEADERS Logger.h ConsoleLogger.h FileLogger.h)
set(UTILS_SOURCES Logger.cpp ConsoleLogger.cpp FileLogger.cpp)

target_sources(${PROJECT_NAME} PRIVATE ${UTILS_HEADERS} ${UTILS_SOURCES})