# ──────────────────────────────────────────
#  NexusChat — C++ Chat Server Makefile
# ──────────────────────────────────────────

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
LDFLAGS  = -lssl -lcrypto -lpthread
TARGET   = chat_server
SRC      = server.cpp

.PHONY: all clean run docker

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
	@echo "✓ Build successful → ./$(TARGET)"

run: $(TARGET)
	@echo "Starting NexusChat server on :8080 ..."
	./$(TARGET)

clean:
	rm -f $(TARGET)

docker:
	docker build -t nexuschat-server .
	docker run -p 8080:8080 nexuschat-server

install-deps-ubuntu:
	sudo apt-get install -y build-essential libssl-dev

install-deps-mac:
	brew install openssl
	# If openssl not found: export PKG_CONFIG_PATH=$(brew --prefix openssl)/lib/pkgconfig
