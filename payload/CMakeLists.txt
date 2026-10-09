# Builds the payload for Quest (arm64). NOT run on this computer's sandbox - it needs the Android NDK.
# The GitHub workflow runs it for you. By hand (Android NDK installed):
#   cmake -S payload -B payload/build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DCMAKE_BUILD_TYPE=Release
#   cmake --build payload/build        ->  payload/build/libmain_payload.so
cmake_minimum_required(VERSION 3.18)
project(timmyzstuff_payload CXX)
set(CMAKE_CXX_STANDARD 17)
add_library(main_payload SHARED src/proxy.cpp ../menu/src/menu_input.cpp)     # produces libmain_payload.so
target_include_directories(main_payload PRIVATE ../menu/src)
target_compile_options(main_payload PRIVATE -fvisibility=hidden)   # export only JNI_OnLoad
target_link_options(main_payload PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
target_link_libraries(main_payload dl log)
