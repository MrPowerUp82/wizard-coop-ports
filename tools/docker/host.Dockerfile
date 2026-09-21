# Host (Linux) toolchain for building/testing arcana_core and the SDL2 desktop frontend.
FROM gcc:14
RUN apt-get update && apt-get install -y --no-install-recommends cmake ninja-build pkg-config \
      libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
