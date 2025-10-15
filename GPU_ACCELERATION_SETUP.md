# GPU Acceleration Setup for Whisper.cpp

## Summary

Successfully enabled **Vulkan GPU acceleration** for your Intel Arc 130V/140V integrated GPU.

### Performance Improvement
- **CPU-only**: encode time = 12,536 ms
- **Vulkan GPU**: encode time = 5,523 ms
- **Speed-up**: 2.27x faster (56% reduction in processing time)

## What Was Done

### 1. Installed Vulkan Development Packages
```bash
sudo apt-get install -y libvulkan-dev glslang-tools spirv-tools libshaderc-dev glslc
```

### 2. Fixed whisper.cpp iGPU Support
**Problem**: whisper.cpp only detected discrete GPUs (`GGML_BACKEND_DEVICE_TYPE_GPU`) and ignored integrated GPUs (`GGML_BACKEND_DEVICE_TYPE_IGPU`).

**Solution**: Patched `src/whisper.cpp` line 1292 to support both:
```cpp
// Support both discrete GPUs and integrated GPUs (iGPU)
if (dev_type == GGML_BACKEND_DEVICE_TYPE_GPU || dev_type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
```

### 3. Rebuilt with Vulkan Support
```bash
cmake -B build -DGGML_VULKAN=1
cmake --build build -j$(nproc) --config Release
```

## Verification

Run whisper-cli and look for:
```
whisper_backend_init_gpu: using Vulkan0 backend
```

Instead of:
```
whisper_backend_init_gpu: no GPU found
```

## Voice Typing Script

Your `voice_typing_shortcut.sh` script now automatically uses GPU acceleration since it calls `./build/bin/whisper-cli`, which is the GPU-accelerated binary.

**Expected improvement**: Voice typing should process audio ~2x faster, reducing the delay between speaking and seeing the text appear.

## System Information

- **GPU**: Intel Arc Graphics 130V/140V (Lunar Lake)
- **Driver**: Intel open-source Mesa driver with Vulkan support
- **Backend**: Vulkan (libvulkan_intel.so)
- **GPU Type**: Integrated GPU (iGPU) with unified memory architecture

## Alternative Acceleration Options (Not Implemented)

If Vulkan has issues, other options include:

1. **SYCL** (Intel oneAPI) - Potentially highest performance but requires ~2GB Intel oneAPI Base Toolkit
2. **OpenVINO** - Intel's inference toolkit, requires model conversion
3. **OpenBLAS** - CPU-only acceleration, slower than GPU options

## Notes

- The Vulkan backend uses GPU memory for computation
- Flash attention is enabled by default (`-fa` flag)
- VAD (Voice Activity Detection) still runs on CPU (GPU VAD is disabled in whisper.cpp for performance reasons)

