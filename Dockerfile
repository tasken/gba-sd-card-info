# syntax=docker/dockerfile:1
FROM devkitpro/devkitarm:latest@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5 AS toolchain
FROM debian:trixie-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a AS slim

FROM slim AS builder
COPY --from=toolchain /opt/devkitpro /opt/devkitpro
ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:$PATH
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
        build-essential ccache python3 ca-certificates zlib1g libzstd1 \
        libtinfo6 libmpc3 libmpfr6 libgmp10 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
COPY Makefile gba.ld README.md LICENSE THIRD_PARTY.md Dockerfile docker-compose.yml docker-build.sh .dockerignore ./
COPY src/ src/
COPY tests/ tests/
COPY tools/ tools/
ARG BUILD_COMMIT
ARG BUILD_TAG
RUN --mount=type=cache,target=/root/.cache/ccache,sharing=locked \
    BUILD_TAG="$BUILD_TAG" make check CC="ccache arm-none-eabi-gcc" BUILD_COMMIT="$BUILD_COMMIT" \
        HOST_FLAGS="-std=c11 -Wall -Wextra -Werror -Isrc -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
    && mkdir -p /artifacts \
    && cp build/gba-sd-card-info.gba /artifacts/ \
    && cp build/gba-sd-card-info.elf build/gba-sd-card-info.map \
          README.md LICENSE THIRD_PARTY.md /artifacts/ \
    && tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
        -czf /artifacts/source.tar.gz \
        Makefile gba.ld src tests tools README.md LICENSE THIRD_PARTY.md \
        Dockerfile docker-compose.yml docker-build.sh .dockerignore

FROM slim AS exporter
COPY --from=builder /artifacts/ /artifacts/
CMD ["/bin/sh", "-c", "cp /artifacts/*.gba /output/"]
