# Unique GPU timestamp accounting regression

From a configured native Linux Vulkan build using Clang and LLD:

```sh
python3 tests/xbox/vulkan/gpu-timestamps-test.py --build-dir build
python3 tests/xbox/vulkan/gpu-timestamps-test.py --build-dir build --thinlto
```

The runner uses `draw.c` compile flags from `compile_commands.json`, includes
the real production translation unit, and links only exercised sections. It
executes the actual `gpu_ts_readback` and `pgraph_vk_begin_command_buffer`.
It also extracts and executes the actual end-recording timestamp block from
`pgraph_vk_finish`. Four deterministic Vulkan entry points substitute query
results and command-recording calls. No GPU, game or firmware is required.

Six cases cover:

- Two synchronous slots revisited at rotation/flush without double counting.
- Deferred results collected independently across all configured slots.
- `VK_NOT_READY` and error results retried; consume only on `VK_SUCCESS`.
- Slot reuse and recording that has not yet been submitted.
- Unsupported timestamps and an idle slot with no pending recording.
- A new recording replacing a previously unavailable measurement.

The fixture checks exact total/render/nonrender durations and render-pass
counts. It asserts queries do not gain `WAIT_BIT` and that result consumption
does not change `frame_submitted`, whose resource-ownership role is separate.
It does not execute full submission/rotation/fence paths or validate a driver.
Their ordering is represented by the regression calls around the actual
accounting code; existing Vulkan synchronization remains unchanged.

The runner also builds the pre-fix `draw.c` from `HEAD` as a negative control.
It must fail the first test with 12,000 ns where 6,000 ns was expected. After
committing the fix, supply a pre-fix revision with `--baseline-ref REV`.
Expected failures have core dumps disabled. Generated sources, binaries and
logs remain under the ignored `build/cloud-vulkan/gpu-timestamps` directory.
