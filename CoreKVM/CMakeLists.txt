CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
PROJECT(CoreKVM C)

SET(CMAKE_C_STANDARD 90)
SET(CMAKE_C_STANDARD_REQUIRED ON)

IF(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    ADD_COMPILE_OPTIONS(-std=c90 -pedantic -Wall -Wextra -Werror)
ELSEIF(MSVC)
    ADD_COMPILE_OPTIONS(/W4 /WX)
ENDIF()

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)

ADD_LIBRARY(corekvm STATIC
    core/KVMBase.c
    core/KVMFrameBuffer.c
)
TARGET_INCLUDE_DIRECTORIES(corekvm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

ENABLE_TESTING()

# Tests may reach internal headers under core/.
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/core)

ADD_EXECUTABLE(test_base tests/test_base.c)
TARGET_LINK_LIBRARIES(test_base corekvm)
ADD_TEST(NAME base COMMAND test_base)

ADD_EXECUTABLE(test_framebuffer tests/test_framebuffer.c)
TARGET_LINK_LIBRARIES(test_framebuffer corekvm)
ADD_TEST(NAME framebuffer COMMAND test_framebuffer)
