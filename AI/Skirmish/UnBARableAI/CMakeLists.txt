### UnBARableAI config

list(
    PREPEND CMAKE_PREFIX_PATH
    "/build/out/cpp/install"
    "/build/out/cpp/Release/build/Release/generators"
)

# RelWithDebInfo soll die Release-Artefakte der importierten Conan-Pakete verwenden.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)

find_package(UnBARableAI CONFIG REQUIRED)

if(NOT TARGET fmt::fmt)
    message(FATAL_ERROR "Expected imported target fmt::fmt is missing")
endif()


set(mySourceDirRel         "src/")
set(additionalSources      "")
set(additionalCompileFlags "-Wall")

set(additionalLibraries
    ${Cpp_AIWRAPPER_TARGET}
    CUtils
    UnBARableAI::unbarableai_core
)


include_directories(
    BEFORE
    ${Cpp_AIWRAPPER_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)


configure_native_skirmish_ai(
    mySourceDirRel
    additionalSources
    additionalCompileFlags
    additionalLibraries
)



target_link_options(UnBARableAI PRIVATE
    -Wl,--no-undefined
    -Wl,--no-as-needed
)