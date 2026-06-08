# ── Build stage ──────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential cmake libssl-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY server.cpp CMakeLists.txt ./

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)

# ── Runtime stage ─────────────────────────────────────────────
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libssl3 ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/chat_server .

EXPOSE 8080
CMD ["./chat_server"]
