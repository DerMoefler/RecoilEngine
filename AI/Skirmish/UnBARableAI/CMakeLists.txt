

### UnBARableAI config
list(PREPEND CMAKE_PREFIX_PATH "/build/out/cpp/install")
list(PREPEND CMAKE_PREFIX_PATH "/build/out/cpp/Release/build/Release/generators")


# Mappe den Engine-Buildtype RelWithDebInfo auf die Release-Artefakte
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)

find_package(UnBARableAI CONFIG REQUIRED)



set(mySourceDirRel         "src/")
set(additionalSources      "")
set(additionalCompileFlags "-Wall")

# WICHTIG:
# Alles, was das finale Plugin wirklich braucht, direkt hier rein.
set(additionalLibraries
    ${Cpp_AIWRAPPER_TARGET}
    CUtils
    UnBARableAI::unbarableai_core
)


include_directories(BEFORE
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


# Build soll sofort fehlschlagen, wenn noch Symbole fehlen
target_link_options(UnBARableAI PRIVATE
    -Wl,--no-undefined
    -Wl,--no-as-needed
)