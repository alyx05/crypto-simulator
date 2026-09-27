# syntax=docker/dockerfile:1

# ---- build stage -----------------------------------------------------------
# Compiles the backend and runs the GoogleTest suite as part of the image
# build itself: if a test fails, `docker build`/`docker compose up` fails
# with it, instead of shipping a broken binary.
FROM gcc:13-bookworm AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        libsqlite3-dev \
        libasio-dev \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN rm -rf build \
    && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)" \
    && ctest --test-dir build --output-on-failure

# ---- runtime stage ----------------------------------------------------------
# Minimal image: just the compiled binary, its runtime shared libs, and the
# seed data. No compiler, no headers, no build tooling.
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/merkelrex ./merkelrex
COPY --from=build /app/data ./data

EXPOSE 18080
CMD ["./merkelrex"]