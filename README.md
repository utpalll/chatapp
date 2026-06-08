# ⬡ NexusChat — C++ WebSocket Chat Application

A real-time chat application with a **C++ WebSocket server** (built from scratch using POSIX sockets) and a sleek dark-themed frontend.

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  Browser (HTML/CSS/JS)                              │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │ Sidebar  │  │  Chat Area   │  │ Users Panel  │ │
│  │ (rooms)  │  │  (messages)  │  │  (online)    │ │
│  └──────────┘  └──────────────┘  └──────────────┘ │
└────────────────────┬────────────────────────────────┘
                     │  WebSocket (RFC 6455)
                     │  ws://host:8080
┌────────────────────▼────────────────────────────────┐
│  C++ Chat Server (server.cpp)                       │
│                                                     │
│  ┌──────────┐   ┌──────────┐   ┌──────────────┐   │
│  │ TCP      │   │ WS       │   │ Per-client   │   │
│  │ Listener │──▶│ Handshake│──▶│ Thread       │   │
│  └──────────┘   └──────────┘   └──────────────┘   │
│                                                     │
│  ┌──────────────────┐  ┌───────────────────────┐   │
│  │ Client Map       │  │ Room History (50 msgs) │   │
│  │ std::map<fd,ptr> │  │ std::map<room,vector>  │   │
│  └──────────────────┘  └───────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

---

## Features

### Server (C++)
- Pure POSIX socket implementation — no external web frameworks
- RFC 6455 WebSocket handshake (SHA-1 + Base64 via OpenSSL)
- Multi-threaded: one `std::thread` per client
- Thread-safe client map with `std::mutex`
- Multiple chat rooms (`general`, `tech`, `random`)
- Room history (last 50 messages replayed on join)
- Typing indicators broadcast
- Online user list per room
- Ping/pong keepalive
- Graceful client disconnect cleanup

### Frontend (HTML/CSS/JS)
- Dark terminal aesthetic with cyan/green neon accents
- Real-time message updates
- Typing indicator animation
- Room switching
- Online users panel
- Color-coded user avatars
- No dependencies — pure vanilla JS

---

## Quick Start (Local)

### 1. Install dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get install build-essential libssl-dev
```

**macOS:**
```bash
brew install openssl
```

### 2. Build the server
```bash
cd server
make
```

### 3. Run the server
```bash
./chat_server
# Output: C++ Chat Server — Port 8080
```

### 4. Open the frontend
```bash
# Just open client/index.html in your browser
open client/index.html      # macOS
xdg-open client/index.html  # Linux
```

Set server URL to `ws://localhost:8080` in the join dialog.

---

## Deployment (Free)

### Option A — Railway (Recommended)

1. Push your code to GitHub
2. Go to [railway.app](https://railway.app) → New Project → Deploy from GitHub
3. Select your repo → Railway auto-detects the Dockerfile
4. Railway gives you a public URL like `https://nexuschat-xxx.railway.app`
5. Your WebSocket URL will be `wss://nexuschat-xxx.railway.app`

> **Note:** Railway's free tier gives 500 hours/month. WebSocket connections work natively.

### Option B — Render

1. Push to GitHub
2. Go to [render.com](https://render.com) → New Web Service
3. Connect your GitHub repo
4. Render reads `render.yaml` automatically
5. Free tier: service spins down after 15 min inactivity

### Option C — Fly.io (Best for always-on)

```bash
# Install flyctl
curl -L https://fly.io/install.sh | sh

# Deploy
cd server
fly launch
fly deploy
```

### Option D — Run Locally with ngrok (Quick demo)

```bash
# Start server
./chat_server

# In another terminal, expose it
ngrok tcp 8080
# Use the URL: ws://0.tcp.ngrok.io:PORT
```

---

## Frontend Deployment (GitHub Pages / Netlify)

The frontend is a single `index.html` — deploy it anywhere:

```bash
# Netlify (drag and drop the client/ folder at netlify.com/drop)
# GitHub Pages: push client/ contents to gh-pages branch
# Vercel: vercel deploy client/
```

Update the default Server URL in `index.html` line:
```html
<input ... value="wss://your-server.railway.app">
```

---

## WebSocket Protocol (JSON Messages)

### Client → Server

```jsonc
// Join a room
{ "type": "join", "username": "alice", "room": "general" }

// Send a message
{ "type": "message", "text": "Hello, world!" }

// Typing notification
{ "type": "typing", "room": "general" }
```

### Server → Client

```jsonc
// Chat message
{ "type": "message", "username": "alice", "text": "...", "time": "14:32:01 UTC", "room": "general" }

// System event
{ "type": "system", "text": "alice joined general", "time": "14:32:01 UTC" }

// Online users
{ "type": "users", "users": ["alice", "bob"], "room": "general" }

// Typing
{ "type": "typing", "username": "alice", "room": "general" }

// Room list
{ "type": "rooms", "rooms": ["general", "tech", "random"] }
```

---

## Project Structure

```
chat-app/
├── server/
│   ├── server.cpp        ← C++ WebSocket server (all logic)
│   ├── Makefile          ← Build system
│   ├── CMakeLists.txt    ← CMake alternative
│   └── Dockerfile        ← Container for deployment
├── client/
│   └── index.html        ← Complete frontend (single file)
├── railway.json          ← Railway deployment config
├── render.yaml           ← Render deployment config
└── README.md
```

---

## Extending the Server

### Add authentication
```cpp
// In handle_message, type == "join":
if (text == "secret123") { cl->authenticated = true; }
```

### Add private messages
```cpp
// New message type: "dm"
// Find target client by username and send directly
```

### Add message persistence (SQLite)
```bash
sudo apt-get install libsqlite3-dev
# Add sqlite3.h and store messages in a .db file
```

---

## Internship Notes

This project demonstrates:
- **Socket programming**: `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()`
- **Multi-threading**: `std::thread`, `std::mutex`, `std::lock_guard`
- **Network protocols**: HTTP upgrade, WebSocket RFC 6455 framing
- **Cryptography**: SHA-1 hash, Base64 encoding (OpenSSL)
- **Client-server architecture**: Stateful connections, broadcast messaging
- **Memory management**: `std::shared_ptr` for client lifetime management
