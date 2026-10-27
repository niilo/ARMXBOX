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
add_executable(armxbox_output_vulkan_test output_vulkan_test.cpp ../output_packet.cpp)
add_dependencies(armxbox_output_vulkan_test output_shaders)
target_link_libraries(armxbox_output_vulkan_test PRIVATE Vulkan::Vulkan)
target_include_directories(armxbox_output_vulkan_test PRIVATE ..)
target_compile_options(armxbox_output_vulkan_test PRIVATE -Wall -Wextra -Werror -Wno-missing-field-initializers)
