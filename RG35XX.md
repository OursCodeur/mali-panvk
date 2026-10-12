# RG35XX Pro PanVK bring-up

This branch runs experimental PanVK on the RG35XX Pro's Mali-G31 MC1 through
stock `/dev/mali0`. It preserves the kernel and boot files. The target tested
on 2026-10-12 has Linux 4.9.170, Ubuntu 22.04 ARM64, 973 MiB RAM and
`mali_kbase r20p0-01rel0`, reporting UK ABI 11.17 and GPU ID `0x7093`.

## Status

The driver reports Vulkan 1.3.358. Unchanged OpenMW Clockwork alpha
`36c2976862455de30daf952170b03bb8234e8a65` rendered and captured its menu through
Vulkan 1.3 on the Mali-G31, then exited successfully. Its capture metadata
reported no renderer fallback. The handheld also displayed the loading screen.
A complete 3D scene has not been verified.

| Check | Result |
|---|---|
| 32768-slot descriptor layout, sampled indices 16383 and 16384 | Passed |
| Partially bound arrays and updates after command recording | Passed |
| Descriptor updates between submissions of the same command buffer | Passed |
| Logical 512x1 and 16x32 workgroups, two groups per dispatch | Passed |
| Shared-memory reduction with multiple barriers | Passed |
| Dynamic rendering clear/readback, 640x480, AFBC disabled | Zero pixel errors |
| Clockwork Vulkan 1.3 menu, 640x480 | Captured successfully |
| Clockwork exterior load | Did not complete; device required restart |
| Guarded interior load | Stopped below the RAM threshold; menu and SSH recovered |
| Vulkan CTS, other GPUs and newer kbase ABIs | Not tested |

The exterior failure's cause is unconfirmed. The saved kernel log did not
establish an OOM kill or GPU fault. Successful menu rendering and these focused
checks do not establish Vulkan conformance or stable gameplay.

A later interior attempt with 512 MiB of temporary swap reached only the
"Uploading Scene" screen. A memory guard stopped it after 37 seconds when
`MemAvailable` reached 94 MiB; kbase reported about 403 MiB of GPU allocations.
The guard restored the firmware menu and SSH remained usable. This establishes
a memory constraint during that attempt, without identifying the original
exterior failure.

## Changes

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
export MESA_VK_WSI_DEBUG=sw
```

`PANVK_DEBUG=noafbc` is required for the tested setup. The isolated AFBC clear
produced a GPU `DATA_INVALID_FAULT` and 307200 bad pixels; disabling AFBC
produced zero errors. Compression support through this kbase path remains
unresolved.

The stock firmware has no DRM device. Presentation used a separately built
Weston 10.0.4 fbdev compositor with Pixman and a Wayland-capable SDL 2.32.10.
`MESA_VK_WSI_DEBUG=sw` selects CPU-copy Wayland presentation; Vulkan rendering
still runs on the Mali. That copy adds CPU and memory traffic to performance
measurements. This repository does not install the compositor or replace
firmware launchers.

The tested driver reports a zero timestamp period. GPU timestamp profiling
was unavailable in Clockwork. Its 42 MiB pipeline cache also exceeded alpha's
32 MiB limit on this 1 GiB device; the driver test did not change alpha.

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

Each compute check validates 1024 invocations twice. The recorded runs had
zero errors; their output is in
[results-2026-10-12.txt](tests/rg35xx/results-2026-10-12.txt). The checks reject
a CPU Vulkan device. They do not cover dispatch-base offsets, every subgroup
operation, every update-after-bind descriptor type or Vulkan CTS.
