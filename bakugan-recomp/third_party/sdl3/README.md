# SDL3 host input

Official SDL 3.4.20 VC development package, used through its C ABI with clang-cl.
CMake downloads the official archive only when no local package is configured,
verifies SHA256 and copies the x64 DLL and license next to the executables.
No global install. Override `BAKUGAN_SDL3_ROOT` for an existing extracted package.

Source/release: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20
Archive SHA256: `1ec3009655ee37b1f73e52b3b120acc5afd8abaf9532f3303b1bf1dfe91b5969`.
Headers, import libraries and binaries are build dependencies; not game assets.
