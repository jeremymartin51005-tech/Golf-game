#!/bin/sh
# Builds GolfCore + bindings to WebAssembly with stock clang and wasi-libc (no Emscripten needed).
# Ubuntu: apt install clang lld wasi-libc libc++-18-dev-wasm32 libc++abi-18-dev-wasm32 libclang-rt-18-dev-wasm32
set -e
cd "$(dirname "$0")"
mkdir -p build
clang++ --target=wasm32-wasi -std=c++17 -O2 -fno-exceptions -fno-rtti -mexec-model=reactor \
  -I../Source/GolfCore/Public \
  ../Source/GolfCore/Private/BallFlight.cpp ../Source/GolfCore/Private/Clubs.cpp ../Source/GolfCore/Private/CourseAssembly.cpp \
  ../Source/GolfCore/Private/CourseGen.cpp ../Source/GolfCore/Private/Impact.cpp ../Source/GolfCore/Private/Round.cpp \
  ../Source/GolfCore/Private/Surface.cpp ../Source/GolfCore/Private/Swing.cpp ../Source/GolfCore/Private/Validation.cpp \
  src/bindings.cpp src/wasm_runtime.cpp -Wl,--strip-all -o build/golfcore.wasm
ls -l build/golfcore.wasm
