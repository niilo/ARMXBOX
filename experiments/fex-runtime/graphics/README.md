# Owned packet offscreen Vulkan test

This separate C++20 executable requires Vulkan headers/loader, a Vulkan driver,
Khronos validation layers, glslangValidator and spirv-val. It does not require
FEX, Android tools or game assets. See [scope and evidence](../../../docs/devel/fex-hle/phase12b.md).

```sh
cmake -S experiments/fex-runtime/graphics -B /tmp/armxbox-output-gpu -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/armxbox-output-gpu -j 2
# Choose the installed software ICD path; vendor paths differ by distribution.
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
timeout 30s /tmp/armxbox-output-gpu/armxbox_output_vulkan_test /tmp/armxbox-output-gpu
python3 experiments/fex-runtime/graphics/check-negatives.py /tmp/armxbox-output-gpu/armxbox_output_vulkan_test /tmp/armxbox-output-gpu
```

The four submissions and wrong-shader controls must pass without validation
errors. The shader directory must be writable for PPM/RGBA evidence images.
Software-driver success is not Android or Turnip acceptance and measures no FPS.
