# Fuzzing guiado por cobertura. Requiere Clang:
#   cmake -S . -B build/fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#         -DICPU_BUILD_FUZZ=ON -DICPU_SANITIZE=ON
#   build/fuzz/fuzz/icpu_fuzz_decoders -max_total_time=120 tests/golden

add_executable(icpu_fuzz_decoders fuzz_decoders.cpp)
target_compile_options(icpu_fuzz_decoders PRIVATE -fsanitize=fuzzer)
target_link_options(icpu_fuzz_decoders PRIVATE -fsanitize=fuzzer)
target_link_libraries(icpu_fuzz_decoders PRIVATE icpu_core)
