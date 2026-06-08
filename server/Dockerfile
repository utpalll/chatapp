FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

RUN apt-get update && apt-get install -y \
    build-essential libssl-dev ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY server.cpp .

RUN g++ -std=c++17 -O2 -pthread -o chat_server server.cpp \
    -Wl,-Bstatic -lssl -lcrypto \
    -Wl,-Bdynamic -lpthread && \
    echo "Build OK"

EXPOSE 8080
CMD ["./chat_server"]
