# Unique GPU timestamp accounting regression

From a configured native Linux Vulkan build using Clang and LLD:

```sh
python3 tests/xbox/vulkan/gpu-timestamps-test.py --build-dir build
python3 tests/xbox/vulkan/gpu-timestamps-test.py --build-dir build --thinlto
```

The runner uses `draw.c` compile flags from `compile_commands.json` and the
real renderer headers. It extracts and executes the unchanged bodies of
`gpu_ts_readback` and `pgraph_vk_begin_command_buffer`. This permits comparison
with the pre-fix revision without compiling unrelated old functions against
renderer fields that have since changed.
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

The runner also builds pre-fix `draw.c` from immutable revision
`f75153dddd03880c0c6fbfea93b2dea8ceb29af9` as a negative control. It must fail
the first test with 12,000 ns where 6,000 ns was expected. Use
`--baseline-ref REV` to select a different pre-fix revision.
Expected failures have core dumps disabled. Generated sources, binaries and
logs remain under the ignored `build/cloud-vulkan/gpu-timestamps` directory.
