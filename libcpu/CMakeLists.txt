PROJECT(libcpu)

ADD_LIBRARY(cpu SHARED
			frontend.cpp
			disasm.cpp
			basicblock.cpp
			function.cpp
			translate.cpp
			translate_all.cpp
			translate_singlestep.cpp
			translate_singlestep_bb.cpp
			tag.cpp
			optimize.cpp
			fp.cpp
			idbg.cpp
			stat.cpp
			sha1.cpp
			interface.cpp
			timings.cpp
			backend_impl.cpp
			backend_llvm.cpp
			llvm_versions.cpp
			backend_qbe_full.cpp
			backend_gccjit_full.cpp
			backend_tcg_full.cpp
			tcg_impl.cpp
			backend_asmjit_full.cpp
			backend_dynasm.cpp
			backend_sljit.cpp
			backend_nanojit.cpp
			backend_mir.cpp
			backend_cranelift.cpp
			backend_libjit.cpp
			backend_nj.cpp
			backend_emulation.cpp
			backend_emulation_types.cpp
			backend_emulation_exotic.cpp
			tiered_compilation.cpp)

ADD_DEFINITIONS(-DLIBCPU_BUILD_CORE)

IF(CMAKE_COMPILER_IS_GNUCXX)
    ADD_DEFINITIONS(-fno-strict-aliasing)
ENDIF()

TARGET_LINK_LIBRARIES(cpu ${GUEST_ARCHITECTURES_ENABLED})
IF(HAVE_LIBREADLINE)
	ADD_DEFINITIONS(-DUSE_READLINE)
	TARGET_LINK_LIBRARIES(cpu readline)
ENDIF()
IF(HAVE_LIBRT)
	TARGET_LINK_LIBRARIES(cpu rt)
ENDIF()
TARGET_LINK_LLVM(cpu)
