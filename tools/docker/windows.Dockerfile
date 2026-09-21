# Windows (x86_64) cross toolchain: MinGW-w64 GCC + the official SDL2 / SDL2_image / SDL2_ttf
# MinGW development packages, for building the desktop frontend (tools/docker/windows-build.sh).
# zip packs the release bundle. The SDL packages land in /opt/sdl-win64 (bin/ has the DLLs).
FROM debian:trixie-slim
ARG SDL2_VERSION=2.32.10
ARG SDL2_IMAGE_VERSION=2.8.12
ARG SDL2_TTF_VERSION=2.24.0
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl cmake ninja-build zip g++-mingw-w64-x86-64-posix \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix \
    && update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
RUN mkdir -p /opt/sdl-win64 /tmp/sdl && cd /tmp/sdl \
    && for pkg in \
         "SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-mingw.tar.gz" \
         "SDL_image/releases/download/release-${SDL2_IMAGE_VERSION}/SDL2_image-devel-${SDL2_IMAGE_VERSION}-mingw.tar.gz" \
         "SDL_ttf/releases/download/release-${SDL2_TTF_VERSION}/SDL2_ttf-devel-${SDL2_TTF_VERSION}-mingw.tar.gz"; do \
         curl -fsSL "https://github.com/libsdl-org/$pkg" | tar xz; \
       done \
    && for d in */x86_64-w64-mingw32; do cp -r "$d"/* /opt/sdl-win64/; done \
    && rm -rf /tmp/sdl
