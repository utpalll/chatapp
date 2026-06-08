FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential libssl-dev ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY server.cpp .

RUN g++ -std=c++17 -O2 -pthread -o chat_server server.cpp \
    -lssl -lcrypto -lpthread && \
    echo "Build OK"

ENV PORT=8080
EXPOSE 8080
CMD ["./chat_server"]
