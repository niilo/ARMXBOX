cmake_minimum_required(VERSION 3.22)
project(ARMXBOXPcmSource LANGUAGES CXX)
# Independent finite-source callback contract; no audio device or FEX required.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
add_library(armxbox_pcm_source STATIC pcm_source.cpp)
target_include_directories(armxbox_pcm_source PUBLIC .)
target_compile_options(armxbox_pcm_source PRIVATE -Wall -Wextra -Werror)
add_executable(armxbox_pcm_source_test pcm_source_test.cpp)
target_link_libraries(armxbox_pcm_source_test PRIVATE armxbox_pcm_source Threads::Threads)
target_compile_options(armxbox_pcm_source_test PRIVATE -Wall -Wextra -Werror)
enable_testing()
add_test(NAME pcm-source COMMAND armxbox_pcm_source_test)

# Compile the real controller against a test-only asynchronous AAudio model.
# No fake backend/header is part of the Android target.
add_executable(armxbox_pcm_sink_test pcm_sink_test.cpp pcm_sink.cpp)
target_include_directories(armxbox_pcm_sink_test PRIVATE test)
target_link_libraries(armxbox_pcm_sink_test PRIVATE armxbox_pcm_source Threads::Threads)
target_compile_options(armxbox_pcm_sink_test PRIVATE -Wall -Wextra -Werror -fexceptions)
target_compile_definitions(armxbox_pcm_sink_test PRIVATE
  ARMXBOX_PCM_DRAIN_TIMEOUT_NS=50000000LL ARMXBOX_PCM_STOP_TIMEOUT_NS=20000000LL ARMXBOX_PCM_POLL_US=50)
add_test(NAME pcm-controller COMMAND armxbox_pcm_sink_test)
