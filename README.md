# VULKAN STRAND-BASED HAIR RENDERER (WIP)

* Available only on Windows (for now).
* Unity's Enemies Hair Pipeline
* Some hair models are created from [Gaussian Haircut](https://github.com/eth-ait/GaussianHaircut).

## PREQUISITES

- Windows 10, 11 (Although it should be easy enough to set it up for Linux).
- Vulkan SDK 1.3.* installed (With VMA and Shaderc).
- CMake installed.
- Git LFS (resources are stored with it)
- Ninja (recommended)

1. Clone the repo:
   ```bash
   git clone --recursive https://github.com/AEspinosaDev/Hair-Renderer-2.git
   cd Hair-Renderer-X
   ```
2. Build with CMake:
   ```bash
   mkdir build
   cd build
   cmake ..
   ```

