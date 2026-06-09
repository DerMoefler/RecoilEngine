### Simple BAR AI config
#

set(mySourceDirRel         "src/")
set(additionalSources      "")
set(additionalCompileFlags "-Wall")
set(additionalLibraries    ${Cpp_AIWRAPPER_TARGET} CUtils)

include_directories(BEFORE
		${Cpp_AIWRAPPER_INCLUDE_DIRS}
		${SDL2_INCLUDE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/src
	)
configure_native_skirmish_ai(mySourceDirRel additionalSources additionalCompileFlags additionalLibraries)
