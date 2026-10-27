cmake_minimum_required(VERSION 3.22)
project(ARMXBOXOutputVulkan LANGUAGES CXX)
# Independent consumer test; no FEX or production renderer dependencies.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Vulkan REQUIRED)
find_program(GLSLANG glslangValidator REQUIRED)
find_program(SPIRV_VAL spirv-val REQUIRED)
foreach(stage vert frag)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/output.${stage}.spv"
    COMMAND "${GLSLANG}" --target-env vulkan1.0 -V "${CMAKE_CURRENT_SOURCE_DIR}/output.${stage}" -o "${CMAKE_CURRENT_BINARY_DIR}/output.${stage}.spv"
    COMMAND "${SPIRV_VAL}" --target-env vulkan1.0 "${CMAKE_CURRENT_BINARY_DIR}/output.${stage}.spv"
    DEPENDS "output.${stage}" VERBATIM)
endforeach()
add_custom_target(output_shaders DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/output.vert.spv" "${CMAKE_CURRENT_BINARY_DIR}/output.frag.spv")
add_library(armxbox_output_vulkan STATIC output_vulkan.cpp)
target_link_libraries(armxbox_output_vulkan PUBLIC Vulkan::Vulkan)
target_include_directories(armxbox_output_vulkan PUBLIC .. .)
target_compile_options(armxbox_output_vulkan PRIVATE -fexceptions -Wall -Wextra -Werror -Wno-missing-field-initializers)
add_executable(armxbox_output_vulkan_test output_vulkan_test.cpp ../output_packet.cpp)
add_dependencies(armxbox_output_vulkan_test output_shaders)
target_link_libraries(armxbox_output_vulkan_test PRIVATE armxbox_output_vulkan)
target_include_directories(armxbox_output_vulkan_test PRIVATE ..)
target_compile_options(armxbox_output_vulkan_test PRIVATE -Wall -Wextra -Werror -Wno-missing-field-initializers)

# Test-only fake device/producer verifies the real noexcept bridge under failures.
# It neither links nor impersonates Vulkan execution in the render test above.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/output_spirv.h"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/embed-spirv.py"
    "${CMAKE_CURRENT_BINARY_DIR}/output.vert.spv" "${CMAKE_CURRENT_BINARY_DIR}/output.frag.spv"
    "${CMAKE_CURRENT_BINARY_DIR}/output_spirv.h"
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/output.vert.spv" "${CMAKE_CURRENT_BINARY_DIR}/output.frag.spv"
    ../scripts/embed-spirv.py VERBATIM)
add_executable(armxbox_output_vulkan_probe_test output_vulkan_probe_test.cpp
  output_vulkan_probe.cpp ../output_packet.cpp "${CMAKE_CURRENT_BINARY_DIR}/output_spirv.h")
target_include_directories(armxbox_output_vulkan_probe_test PRIVATE .. . "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(armxbox_output_vulkan_probe_test PRIVATE -fexceptions -Wall -Wextra -Werror)
