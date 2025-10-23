PROJECT(libcpu-tools)

# Static recompiler tool
ADD_EXECUTABLE(static-recompile static-recompile.cpp)
TARGET_LINK_LIBRARIES(static-recompile cpu ${GUEST_ARCHITECTURES_ENABLED})
TARGET_LINK_LLVM(static-recompile)

INSTALL(TARGETS static-recompile DESTINATION bin)
