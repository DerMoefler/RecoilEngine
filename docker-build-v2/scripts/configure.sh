#!/bin/bash
CONAN_GENERATORS=/build/out/cpp/Release/build/Release/generators
UNBARABLE_INSTALL=/build/out/cpp/install

cmake --fresh -S /build/src -B /build/out \
    -DCMAKE_INSTALL_PREFIX:PATH=/build/out/install \
    -DAI_EXCLUDE_REGEX="^CppTestAI$" \
    -DUSERDOCS_PLAIN=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -g -DNDEBUG" \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -g -DNDEBUG" \
    -Dfmt_DIR="${CONAN_GENERATORS}" \
    -DCMAKE_PREFIX_PATH="${CONAN_GENERATORS};${UNBARABLE_INSTALL}" \
    -G Ninja \
    "$@"
