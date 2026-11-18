FROM docker.io/library/ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libc6-dev \
    libstdc++-14-dev \
    libgcc-14-dev \
    libgl1-mesa-dev \
    libegl-dev \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxi-dev \
    libxtst-dev \
    libxinerama-dev \
    libxkbcommon-dev \
    libwayland-dev \
    wayland-protocols \
    libasound2-dev \
    libpulse-dev \
    && rm -rf /var/lib/apt/lists/*
