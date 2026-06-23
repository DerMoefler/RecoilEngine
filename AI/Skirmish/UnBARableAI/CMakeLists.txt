execute_process(
    COMMAND /usr/bin/ldd --version
    OUTPUT_VARIABLE _ldd_out
    ERROR_VARIABLE _ldd_err
)
message(STATUS "CONTAINER_LDD=${_ldd_out}")

execute_process(
    COMMAND /bin/sh -lc "nm -D /lib/x86_64-linux-gnu/libc.so.6 | grep __libc_single_threaded || true"
    OUTPUT_VARIABLE _libc_single_out
    ERROR_VARIABLE _libc_single_err
)
message(STATUS "CONTAINER_LIBC_SINGLE=${_libc_single_out}")

### UnBARableAI config
list(PREPEND CMAKE_PREFIX_PATH "/build/out/cpp/install")
list(PREPEND CMAKE_PREFIX_PATH "/build/cpp/build/Release/generators")


# Mappe den Engine-Buildtype RelWithDebInfo auf die Release-Artefakte
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)

find_package(UnBARableAI CONFIG REQUIRED)

get_target_property(_grpc_type gRPC::grpc++ TYPE)
get_target_property(_grpc_loc gRPC::grpc++ IMPORTED_LOCATION_RELEASE)
get_target_property(_grpc_iface gRPC::grpc++ INTERFACE_LINK_LIBRARIES)

get_target_property(_pb_type protobuf::libprotobuf TYPE)
get_target_property(_pb_loc protobuf::libprotobuf IMPORTED_LOCATION_RELEASE)
get_target_property(_pb_iface protobuf::libprotobuf INTERFACE_LINK_LIBRARIES)

message(STATUS "gRPC::grpc++ TYPE=${_grpc_type}")
message(STATUS "gRPC::grpc++ IMPORTED_LOCATION_RELEASE=${_grpc_loc}")
message(STATUS "gRPC::grpc++ INTERFACE_LINK_LIBRARIES=${_grpc_iface}")

message(STATUS "protobuf::libprotobuf TYPE=${_pb_type}")
message(STATUS "protobuf::libprotobuf IMPORTED_LOCATION_RELEASE=${_pb_loc}")
message(STATUS "protobuf::libprotobuf INTERFACE_LINK_LIBRARIES=${_pb_iface}")

set(mySourceDirRel         "src/")
set(additionalSources      "")
set(additionalCompileFlags "-Wall")

# WICHTIG:
# Alles, was das finale Plugin wirklich braucht, direkt hier rein.
set(additionalLibraries
    ${Cpp_AIWRAPPER_TARGET}
    CUtils
    UnBARableAI::unbarableai_core
    CONAN_LIB::grpc_gRPC_grpc++_grpc++_RELEASE
    CONAN_LIB::protobuf_protobuf_libprotobuf_protobuf_RELEASE
)


include_directories(BEFORE
    ${Cpp_AIWRAPPER_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

message(STATUS "UnBARableAI additionalLibraries=${additionalLibraries}")

configure_native_skirmish_ai(
    mySourceDirRel
    additionalSources
    additionalCompileFlags
    additionalLibraries
)

get_target_property(_ubar_link_libs UnBARableAI LINK_LIBRARIES)
message(STATUS "UnBARableAI LINK_LIBRARIES=${_ubar_link_libs}")

# Build soll sofort fehlschlagen, wenn noch Symbole fehlen
target_link_options(UnBARableAI PRIVATE
    -Wl,--no-undefined
    -Wl,--no-as-needed
)