find_library(SECURITY_FRAMEWORK Security)
set(PLATFORM_SOURCES src/sys/macos/MacOS.cpp src/sys/macos/AutoRun.cpp src/sys/macos/UrlScheme.cpp)
set(PLATFORM_LIBRARIES ${SECURITY_FRAMEWORK})