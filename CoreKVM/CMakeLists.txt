CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
PROJECT(CoreKVM C)

SET(CMAKE_C_STANDARD 90)
SET(CMAKE_C_STANDARD_REQUIRED ON)
SET(CMAKE_C_EXTENSIONS OFF)

IF(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    ADD_COMPILE_OPTIONS(-std=c90 -pedantic -Wall -Wextra -Werror)
ELSEIF(MSVC)
    ADD_COMPILE_OPTIONS(/W4 /WX)
ENDIF()

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)

ADD_LIBRARY(corekvm STATIC
    core/KVMBase.c
    core/KVMFrameBuffer.c
    core/KVMDirty.c
    core/KVMInput.c
    core/KVMBuffer.c
    core/KVMEvent.c
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

ADD_EXECUTABLE(test_pixelconvert tests/test_pixelconvert.c)
TARGET_LINK_LIBRARIES(test_pixelconvert corekvm)
ADD_TEST(NAME pixelconvert COMMAND test_pixelconvert)

ADD_EXECUTABLE(test_dirty tests/test_dirty.c)
TARGET_LINK_LIBRARIES(test_dirty corekvm)
ADD_TEST(NAME dirty COMMAND test_dirty)

ADD_EXECUTABLE(test_input tests/test_input.c)
TARGET_LINK_LIBRARIES(test_input corekvm)
ADD_TEST(NAME input COMMAND test_input)

ADD_EXECUTABLE(test_buffer tests/test_buffer.c)
TARGET_LINK_LIBRARIES(test_buffer corekvm)
ADD_TEST(NAME buffer COMMAND test_buffer)

ADD_EXECUTABLE(test_event tests/test_event.c)
TARGET_LINK_LIBRARIES(test_event corekvm)
ADD_TEST(NAME event COMMAND test_event)
