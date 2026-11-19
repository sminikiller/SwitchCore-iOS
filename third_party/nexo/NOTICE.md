# NeXo 2

Source snapshot from NeXoNetwork/NeXo2-emu, commit d329d28bfeb3bfab175c4c380aa926679446c1c9. The C++ engine, unit tests, guest programs and generated fixtures retain their original GPLv3 license. See LICENSE and the source comments. Desktop UI, JIT dependencies, TLS dependencies, artwork and Vulkan libraries are excluded from the build.

SwitchCore adaptations cover the first-generation counter, scheduler and audio timing, mobile input validation and Apple toolchain portability. In the iOS Simulator the Vulkan device asks MoltenVK to bind descriptors without Metal argument buffers. The Vulkan rasterizer numbers the vertex shader bindings after the texture bindings, because repeated binding numbers left the translated vertex shader without its buffers under MoltenVK. The NSO and extracted application loaders check segment layout, image budget, file sizes, symbolic links and Switch 2 program IDs. The display copies RGBA8 framebuffers directly into the presented frame. [Upstream source](https://github.com/NeXoNetwork/NeXo2-emu/tree/d329d28bfeb3bfab175c4c380aa926679446c1c9)

On Apple hosts the shared font service can load Atkinson Hyperlegible Regular from the application bundle. Copyright Braille Institute of America, Inc. The unmodified font and SIL Open Font License are included in apps/ios/Resources. [Font source](https://github.com/google/fonts/tree/1b22086d13bdbbf5ec63d01a39a49246e4ac355f/ofl/atkinsonhyperlegible)

