<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Shelved surface revival regression

Run from the repository root without a GPU, firmware or game image:

```sh
python3 tests/xbox/vulkan/surface-revival-test.py \
  --sanitize --negative-control
```

The runner extracts the complete production `download_surface`,
`shelve_surface`, `get_shelved_surface`, `migrate_surface_image` and
`update_surface_part` functions. It compiles those unchanged bodies with
deterministic guest VRAM, image-transfer and callback boundaries. A separate
byte oracle checks the resulting image contents. Assertions stay enabled.

The fixture runs 1,536 surface lifecycles covering color/depth surfaces,
clean/dirty surfaces with partial GPU writes, and full/partial/no CPU writes
during shelving. It verifies that dirty contents reach VRAM before the CPU
access callback is removed, then that revival refreshes the reused image from
VRAM. It also checks six mismatched shelf keys, image/view identity reuse,
pixel preservation outside a later partial clear, and the existing no-upload
behavior for an active compatible cache hit.

The transfer stubs implement the existing pending-upload contract; they do not
execute Vulkan commands. Driver behavior, real GPU synchronization and device
performance require device testing. This fixture does not establish that SSX 3
uses the reproduced shelf/write/revival sequence.

The negative control extracts the same complete functions from immutable RC7
commit `d2da241bab37c6460fc8832e8ded75d42362e739`. It must fail with
`revived surface skipped authoritative VRAM upload`, after a partial CPU write
made while the callback is unregistered. Compilation failures do not count as
a reproduced defect. Use `--baseline-ref REV` to select another revision.

Revival still reuses the cached Vulkan allocation and view. It now pays for
one upload on revival, because untracked CPU writes make the old pixel contents
unsafe to reuse. Ordinary active-cache hits retain their existing behavior.
