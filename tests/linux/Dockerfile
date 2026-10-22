# Build and runtime tests run under Linux x64 emulation on Apple silicon.
# Build context is tests/linux/, not the checkout or the user's home directory.
FROM ubuntu:22.04@sha256:0a9887931b0e38af2e3487ed39e29436b0535c4f3f52e57745baaf79d1898956
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates curl unzip jq zstd python3 file \
    libglu1-mesa-dev libx11-dev libxrandr2 libxinerama1 libxcursor1 libxi6 \
    libasound2 libgtk2.0-0 libgtk-3-0 libjack-jackd2-0 libpulse0 \
    zenity xvfb xauth mesa-utils \
    && rm -rf /var/lib/apt/lists/*
RUN mkdir -p /opt/knobon && cd /opt/knobon \
    && curl -fL https://vcvrack.com/downloads/Rack-SDK-2.6.6-lin-x64.zip -o sdk.zip \
    && curl -fL https://vcvrack.com/downloads/RackFree-2.6.6-lin-x64.zip -o rack.zip \
    && sha256sum sdk.zip rack.zip > download-sha256.txt \
    && unzip -q sdk.zip && unzip -q rack.zip && rm sdk.zip rack.zip
ENV RACK_LINUX_SDK=/opt/knobon/Rack-SDK
ENV LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /work
