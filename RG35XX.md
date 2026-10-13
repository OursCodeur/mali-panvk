# RG35XX Pro PanVK bring-up

This branch runs experimental PanVK on the RG35XX Pro's Mali-G31 MC1 through
stock `/dev/mali0`. It preserves the kernel and boot files. The target tested
on 2026-10-12 has Linux 4.9.170, Ubuntu 22.04 ARM64, 973 MiB RAM and
`mali_kbase r20p0-01rel0`, reporting UK ABI 11.17 and GPU ID `0x7093`.

## Status

The driver reports Vulkan 1.3.358. Unchanged OpenMW Clockwork alpha
`36c2976862455de30daf952170b03bb8234e8a65` renders the Census and Excise Office
interior on the Mali-G31, including its walls, floor and ceiling. Engine and
physical framebuffer captures confirm the room geometry. The owner confirmed
controls and audio in a subsequent interior run, which exited normally.
The delayed exterior guardian eventually recovered the device without a
restart. Subsequent exterior attempts stopped at the physical-memory guard.
No exterior run has passed acceptance, and the normal launcher still starts
the installed OpenGL build.

| Check | Result |
|---|---|
| 16384-slot descriptor layout, sampled indices 16382 and 16383 | Passed |
| Partially bound arrays and updates after command recording | Passed |
| Descriptor updates between submissions of the same command buffer | Passed |
| Logical 512x1 and 16x32 workgroups, two groups per dispatch | Passed |
| Shared-memory reduction with multiple barriers | Passed |
| Dynamic rendering clear/readback, 640x480, AFBC disabled | Zero pixel errors |
| Direct and indirect indexed draws with instanced attributes | 24 cases; 98,304 pixels; zero errors |
| Instance divisors zero, one and three | Passed |
| Clockwork Vulkan 1.3 interior, 640x480 | Correct room captured; normal exit |
| Physical controls and audio | Confirmed by the owner |
| Clockwork exterior | Physical-memory guard stops loading; no accepted run |
| Vulkan CTS, other GPUs and newer kbase ABIs | Not tested |

These focused checks do not establish Vulkan conformance or sustained gameplay
across scenes. The verified interior used temporary swap and VM reclaim
settings. The successful interior launcher restored those VM settings at
exit. Forced termination during an exterior test required manual framebuffer
and menu restoration.

The tested ARM64 driver binary has SHA-256
`c039e0c83012a4a8c27c9559c2dfc281f59d2561bffecc5ecbbc734761cb4638`.

## Memory and recovery

The exterior attempt with 512 MiB swap exhausted that swapfile. Its guardian
stopped the game after 366.8 seconds, following a 96.5-second gap in diagnostic
samples. SSH and the firmware menu recovered without a restart.

An independent C watchdog now locks its address space into RAM. It checks
`/proc/meminfo` and sends stop signals without waiting for GPU debug reads or
SD-card writes. The runtime has an extra temporary 256 MiB swapfile, for
768 MiB total.
The watchdog and swapfiles do not change this driver's code or the boot setup.

Reducing the reclaim reserve still let the next exterior attempt reach the
physical-memory guard. Another attempt with 768 MiB swap also hit that guard,
with about 290 MiB swap still free and 467.12 MiB of Mali allocations. More
swap alone has not produced an accepted exterior. The stock launcher remains
unchanged.

An interior capture with PNG dumping disabled recorded:

| Counter | MiB |
|---|---:|
| Ordinary process resident pages | 247.30 |
| Process swap | 289.96 |
| Mali committed pages | 367.24 |

These counters measure different memory classes. The Mali counter is not a
measure of ordinary process residency. GPU clocks varied between 420 and
648 MHz; the capture does not support a fixed-clock utilization claim.
The driver still provides no valid Vulkan timestamps for per-pass timing.

The Clockwork results above use alpha
`36c2976862455de30daf952170b03bb8234e8a65`. The owner approved a separate
renderer memory patch against development commit
`84de784caf8c421f0ba18954de3af3536fa3c83c`. That newer baseline needs its own
validation; the results above do not describe it. The renderer patch is
separate from this driver fork.

## Changes

- Limit per-stage sampled-image and sampler tables to 16384 entries. Alpha
  accepts this limit and uses a 16326-entry array. Its saved pipeline cache is
  24 MiB, within the existing 32 MiB cap on this device.
- Initialize the optional fragment descriptor-copy job before preparing a
  tiler job. Depth-only command-buffer resubmission no longer consumes an
  uninitialized pointer.
- Cancel the indexed vertex offset when fetching instanced attributes in
  direct draws and indirect helper jobs. Apply the correction for multiple
  instances with nonzero divisors; zero-divisor attributes remain constant.
- Use the 48-byte job atom required by the tested kbase UK 11.17 interface.
  This replaces upstream's 64-byte layout; newer Android kernels are untested.
- Use four-lane subgroups and the older lane-permute instruction on the G31.
- Copy sampled-image update-after-bind descriptor tables on the GPU. Stable
  descriptor sets retain upstream's CPU copy path. Copies use bounded groups
  of 64 invocations and support larger descriptor tables.
- Expose sampled-image update-after-bind, partially bound descriptors and
  update-unused-while-pending. Other descriptor types retain their limits;
  the aggregate `descriptorIndexing` feature remains disabled on Bifrost.
- Lower logical workgroups larger than the physical 256-invocation limit with
  NIR, preserving the tested shared-memory and barrier behavior.
- Restore the Linux Wayland loader sources and Vulkan Wayland header omitted
  from the Android source snapshot.

## Provenance

The branch starts at [JimVulkan/mali-panvk
`fb3b49d`](https://github.com/JimVulkan/mali-panvk/commit/fb3b49d016544c19d83e558462f2d29470e77c28).
Existing commits and source license notices remain intact.

The nine `src/loader` files come from [Mesa
`e7959c6`](https://github.com/chaotic-cx/mesa-mirror/tree/e7959c6009d85d278585b2896495b2b3046d461b/src/loader).
`include/vulkan/vulkan_wayland.h` comes from [Vulkan-Headers
v1.4.358](https://github.com/KhronosGroup/Vulkan-Headers/blob/v1.4.358/include/vulkan/vulkan_wayland.h).
[RG35XX-sources.json](RG35XX-sources.json) records their Git blob hashes.

## Build

Build the host shader tools from this branch before the ARM64 driver. The
working setup used Meson 1.11, Ninja 1.13, GCC 13.3 for ARM64, an Ubuntu 22.04
ARM64 sysroot, LLVM/libclc 22.1.8 and SPIR-V LLVM translator 22.1.6. Host
LLVM, clang, libclc and the translator must use the same major version.
Python needs Mako, packaging and PyYAML; Bison, Flex and glslangValidator are
also needed. Use an ARM64 sysroot with the target development libraries,
including zlib, libudev and Wayland. Meson can fetch the declared fallbacks.

The target used Wayland 1.20.0 with a native `wayland-scanner` 1.20.0 and
wayland-protocols 1.49. The scanner version must match the target Wayland
library. Make the matching compiler tools and libraries visible to the host
before configuring.

From the repository root:

```sh
meson setup build-host \
  -Dbuildtype=release -Db_ndebug=true \
  -Dmesa-clc=enabled -Dprecomp-compiler=enabled -Dllvm=enabled \
  -Dvulkan-drivers=panfrost -Dgallium-drivers= -Dplatforms= \
  -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled \
  -Dgbm=disabled -Dtools= -Dvideo-codecs= -Dbuild-tests=false \
  -Dpanfrost-rust=false
ninja -C build-host src/compiler/clc/mesa_clc \
  src/compiler/spirv/vtn_bindgen2 src/panfrost/clc/panfrost_compile
export PATH="$PWD/build-host/src/compiler/clc:$PWD/build-host/src/compiler/spirv:$PWD/build-host/src/panfrost/clc:$PATH"
```

Create `aarch64.ini` for your ARM64 Linux compiler and sysroot. Set its
`sys_root` and `pkg_config_libdir` to the target sysroot, and pass the
appropriate `--sysroot` and linker search paths to that compiler. The tested
CPU option was `-mcpu=cortex-a53`. Keep host libraries out of target package
lookup. The SDK itself is not bundled in this repository.

```sh
meson setup build-arm64 --cross-file aarch64.ini \
  -Dbuildtype=debugoptimized -Db_ndebug=false \
  -Dvulkan-drivers=panfrost -Dgallium-drivers= -Dplatforms=wayland \
  -Dllvm=disabled -Dmesa-clc=system -Dprecomp-compiler=system \
  -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled \
  -Dgbm=disabled -Dtools= -Dvideo-codecs= -Dbuild-tests=false \
  -Dpanfrost-rust=false -Dlibunwind=disabled -Dlmsensors=disabled \
  -Dzstd=disabled --force-fallback-for=libdrm \
  -Dlibdrm:default_library=static
ninja -C build-arm64 src/panfrost/vulkan/libvulkan_panfrost.so
```

When changing the shader compiler, rebuild the host tools and regenerate the
ARM64 precompiled helper shaders. A stale helper can retain a G31 instruction
that the runtime compiler no longer emits.

## Runtime

Keep this ICD in an application directory and select it per process. The
tested setup used Vulkan Loader 1.4.357. For example, place the shared library
under `lib/` beside this `panvk.json`:

```json
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "./lib/libvulkan_panfrost.so",
    "api_version": "1.3.358"
  }
}
```

Set these variables for the application:

```sh
export VK_DRIVER_FILES="$PWD/panvk.json"
export PANVK_DEBUG=noafbc
export MESA_VK_WSI_DEBUG=sw,linear
```

`PANVK_DEBUG=noafbc` is required for the tested setup. The isolated AFBC clear
produced a GPU `DATA_INVALID_FAULT` and 307200 bad pixels; disabling AFBC
produced zero errors. Compression support through this kbase path remains
unresolved.

The stock firmware has no DRM device. Presentation used a separately built
Weston 10.0.4 fbdev compositor with Pixman and a Wayland-capable SDL 2.32.10.
`MESA_VK_WSI_DEBUG=sw,linear` selects CPU-copy Wayland presentation with linear
images. Vulkan rendering still runs on the Mali. Linear images remove the GPU
image-to-buffer copy; the CPU copy to the compositor remains. This repository
does not install the compositor or replace firmware launchers.

The tested driver reports a zero timestamp period. GPU timestamp profiling
was unavailable in Clockwork. The driver changes do not modify alpha's
renderer, shaders or executable.

## Focused checks

[tests/rg35xx](tests/rg35xx) contains the standalone checks used for this
bring-up. Compile `vkcheck.c` and `vkgraphics.c` for the target against Vulkan
headers and its Vulkan loader. The sources are diagnostic programs, separate
from Mesa's test suite. Generate their compute shaders on the build host:

```sh
mkdir -p build-checks
glslangValidator -V --target-env vulkan1.3 \
  -DNO_SAMPLE=1 tests/rg35xx/descriptor-check.comp \
  -o build-checks/arithmetic-512.spv
glslangValidator -V --target-env vulkan1.3 \
  tests/rg35xx/descriptor-check.comp -o build-checks/descriptor-512.spv
glslangValidator -V --target-env vulkan1.3 -DGROUP_X=16 -DGROUP_Y=32 \
  tests/rg35xx/descriptor-check.comp -o build-checks/descriptor-16x32.spv
```

On the target, with the ICD selected and AFBC disabled:

```sh
./vkcheck arithmetic-512.spv 512 1 arithmetic
./vkcheck descriptor-512.spv 512 1
./vkcheck descriptor-16x32.spv 16 32
./vkgraphics 640 480
```

Each compute check validates 1024 invocations twice. The 16384-slot runs had
zero errors; their output is in [results-16k.txt](tests/rg35xx/results-16k.txt).
[results-2026-10-12.txt](tests/rg35xx/results-2026-10-12.txt) records the earlier
32768-slot configuration. The current sources use indices 16382 and 16383.

The indexed-draw matrix covered direct and indirect submission, divisors zero,
one and three, and four `(vertexOffset, minimumIndex)` pairs: `(0, 0)`,
`(25601, 0)`, `(-513, 513)` and `(25601, 37)`. Each of its 24 cases submitted the
command buffer twice and checked 2048 pixels per submission. All 98,304 pixel
checks passed. The recorded output is in
[instance-results.txt](tests/rg35xx/instance-results.txt); each `CASE` line lists
the divisor, direct-draw flag, vertex offset, minimum index and exit status.

The checks reject a CPU Vulkan device. They do not cover dispatch-base offsets,
every subgroup operation, every update-after-bind descriptor type or Vulkan CTS.
