/*
 * ============================================================
 *  Chat Server — C++ WebSocket (RFC 6455) from Scratch
 *  No external libs needed — pure POSIX sockets + threads
 * ============================================================
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <memory>
#include <functional>
#include <queue>
#include <condition_variable>
#include <atomic>

// POSIX
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

// SHA-1 for WebSocket handshake
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

// ── Configuration ────────────────────────────────────────────
static const int    BACKLOG       = 64;
static const size_t MAX_MSG_BYTES = 65536;
static const char*  WS_MAGIC      = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// ── Utilities ────────────────────────────────────────────────
std::string base64_encode(const unsigned char* data, size_t len) {
    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, (int)len);
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

std::string ws_accept_key(const std::string& client_key) {
    std::string combined = client_key + WS_MAGIC;
    unsigned char sha1[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()),
         combined.size(), sha1);
    return base64_encode(sha1, SHA_DIGEST_LENGTH);
}

std::string timestamp() {
    time_t now = time(nullptr);
    tm* t = gmtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S UTC", t);
    return buf;
}

// simple JSON escape
std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

// ── WebSocket Frame ──────────────────────────────────────────
std::vector<uint8_t> build_ws_frame(const std::string& payload, uint8_t opcode = 0x01) {
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode);   // FIN + opcode
    size_t len = payload.size();
    if (len < 126) {
        frame.push_back((uint8_t)len);
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((len >> (8*i)) & 0xFF);
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

bool send_ws_frame(int fd, const std::string& payload, uint8_t opcode = 0x01) {
    auto frame = build_ws_frame(payload, opcode);
    ssize_t sent = 0, total = (ssize_t)frame.size();
    while (sent < total) {
        ssize_t r = send(fd, frame.data() + sent, total - sent, MSG_NOSIGNAL);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

// ── Client ───────────────────────────────────────────────────
struct Client {
    int         fd;
    std::string username;
    std::string room;
    bool        handshaked = false;
    std::string recv_buf;
    std::mutex  send_mtx;

    explicit Client(int f) : fd(f) {}
    ~Client() { if (fd >= 0) close(fd); }

    bool send(const std::string& json) {
        std::lock_guard<std::mutex> lk(send_mtx);
        return send_ws_frame(fd, json);
    }
};

// ── Server State ─────────────────────────────────────────────
std::mutex                               clients_mtx;
std::map<int, std::shared_ptr<Client>>   clients;

// recent 50 messages per room
std::mutex                               history_mtx;
std::map<std::string, std::vector<std::string>> room_history;

std::atomic<bool> running{true};

// ── Broadcast ────────────────────────────────────────────────
void broadcast(const std::string& json, const std::string& room,
               int exclude_fd = -1) {
    std::lock_guard<std::mutex> lk(clients_mtx);
    for (auto& [fd, cl] : clients) {
        if (fd == exclude_fd) continue;
        if (cl->handshaked && cl->room == room)
            cl->send(json);
    }
}

void push_history(const std::string& room, const std::string& json) {
    std::lock_guard<std::mutex> lk(history_mtx);
    auto& h = room_history[room];
    h.push_back(json);
    if (h.size() > 50) h.erase(h.begin());
}

void send_history(std::shared_ptr<Client> cl) {
    std::lock_guard<std::mutex> lk(history_mtx);
    auto& h = room_history[cl->room];
    for (auto& msg : h) cl->send(msg);
}

std::vector<std::string> rooms_list() {
    std::lock_guard<std::mutex> lk(clients_mtx);
    std::map<std::string,int> counts;
    for (auto& [fd,cl] : clients)
        if (cl->handshaked && !cl->room.empty())
            counts[cl->room]++;
    std::vector<std::string> r;
    for (auto& [room,cnt] : counts)
        r.push_back(room);
    return r;
}

std::vector<std::string> users_in_room(const std::string& room) {
    std::lock_guard<std::mutex> lk(clients_mtx);
    std::vector<std::string> u;
    for (auto& [fd,cl] : clients)
        if (cl->handshaked && cl->room == room)
            u.push_back(cl->username);
    return u;
}

void send_user_list(const std::string& room) {
    auto users = users_in_room(room);
    std::string arr = "[";
    for (size_t i = 0; i < users.size(); i++) {
        arr += "\"" + json_escape(users[i]) + "\"";
        if (i+1 < users.size()) arr += ",";
    }
    arr += "]";
    std::string json = "{\"type\":\"users\",\"users\":" + arr +
                       ",\"room\":\"" + json_escape(room) + "\"}";
    broadcast(json, room);
}

// ── HTTP Upgrade + WS Handshake ──────────────────────────────
bool do_handshake(int fd, std::string& buf) {
    while (true) {
        char tmp[4096];
        ssize_t n = recv(fd, tmp, sizeof(tmp)-1, 0);
        if (n <= 0) return false;
        buf.append(tmp, n);
        if (buf.find("\r\n\r\n") != std::string::npos) break;
        if (buf.size() > 16384) return false;
    }

    // Plain HTTP GET (Railway health check) → respond 200 and close
    if (buf.find("Upgrade: websocket") == std::string::npos &&
        buf.find("upgrade: websocket") == std::string::npos) {
        const char* ok =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "OK";
        send(fd, ok, strlen(ok), MSG_NOSIGNAL);
        return false;
    }

    // Extract Sec-WebSocket-Key
    std::string key;
    auto pos = buf.find("Sec-WebSocket-Key:");
    if (pos == std::string::npos) return false;
    pos += 18;
    while (pos < buf.size() && buf[pos] == ' ') pos++;
    auto end = buf.find("\r\n", pos);
    if (end == std::string::npos) return false;
    key = buf.substr(pos, end - pos);

    std::string accept = ws_accept_key(key);
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";

    ssize_t sent = send(fd, response.c_str(), response.size(), MSG_NOSIGNAL);
    buf.clear();
    return sent == (ssize_t)response.size();
}

// ── WS Frame Parser ──────────────────────────────────────────
// Returns decoded payload string, or "" if incomplete/error
// Sets consumed = bytes consumed from buf
bool parse_ws_frame(std::string& buf, std::string& payload, bool& should_close) {
    should_close = false;
    if (buf.size() < 2) return false;

    uint8_t b0 = buf[0], b1 = buf[1];
    // bool fin   = b0 & 0x80;
    uint8_t opcode = b0 & 0x0F;
    bool masked    = b1 & 0x80;
    size_t plen    = b1 & 0x7F;
    size_t offset  = 2;

    if (opcode == 0x08) { should_close = true; return true; }
    if (opcode == 0x09) { // ping → pong
        payload = "";
        return true;
    }

    if (plen == 126) {
        if (buf.size() < 4) return false;
        plen = ((uint8_t)buf[2] << 8) | (uint8_t)buf[3];
        offset = 4;
    } else if (plen == 127) {
        if (buf.size() < 10) return false;
        plen = 0;
        for (int i = 2; i < 10; i++) plen = (plen << 8) | (uint8_t)buf[i];
        offset = 10;
    }

    size_t mask_start = offset;
    if (masked) offset += 4;
    if (buf.size() < offset + plen) return false;
    if (plen > MAX_MSG_BYTES) { should_close = true; return true; }

    payload.resize(plen);
    for (size_t i = 0; i < plen; i++) {
        payload[i] = buf[offset + i];
        if (masked) payload[i] ^= buf[mask_start + (i % 4)];
    }
    buf.erase(0, offset + plen);
    return true;
}

// ── Message Handler ──────────────────────────────────────────
// Minimal JSON parser — find value of key in flat JSON
std::string json_get(const std::string& j, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = j.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < j.size() && (j[pos]==' '||j[pos]==':')) pos++;
    if (pos >= j.size()) return "";
    if (j[pos] == '"') {
        pos++;
        std::string val;
        while (pos < j.size() && j[pos] != '"') {
            if (j[pos]=='\\' && pos+1<j.size()) { pos++; val+=j[pos]; }
            else val += j[pos];
            pos++;
        }
        return val;
    }
    auto end = j.find_first_of(",}", pos);
    return j.substr(pos, end==std::string::npos ? j.size()-pos : end-pos);
}

void handle_message(std::shared_ptr<Client> cl, const std::string& raw) {
    std::string type = json_get(raw, "type");

    if (type == "join") {
        std::string uname = json_get(raw, "username");
        std::string room  = json_get(raw, "room");
        if (uname.empty() || uname.size() > 32) uname = "anon";
        if (room.empty()  || room.size()  > 32) room  = "general";

        std::string old_room = cl->room;
        cl->username = uname;
        cl->room     = room;

        // Notify old room
        if (!old_room.empty() && old_room != room) {
            std::string msg = "{\"type\":\"system\",\"text\":\""
                + json_escape(uname) + " left the room\","
                "\"time\":\"" + timestamp() + "\","
                "\"room\":\"" + json_escape(old_room) + "\"}";
            broadcast(msg, old_room);
            send_user_list(old_room);
        }

        // Send history
        send_history(cl);

        // Welcome message
        std::string sys = "{\"type\":\"system\","
                          "\"text\":\"" + json_escape(uname) + " joined " + json_escape(room) + "\","
                          "\"time\":\"" + timestamp() + "\","
                          "\"room\":\"" + json_escape(room) + "\"}";
        broadcast(sys, room);
        send_user_list(room);

        // Send room list to joiner
        auto rlist = rooms_list();
        std::string rarr = "[";
        for (size_t i = 0; i < rlist.size(); i++) {
            rarr += "\"" + json_escape(rlist[i]) + "\"";
            if (i+1 < rlist.size()) rarr += ",";
        }
        rarr += "]";
        cl->send("{\"type\":\"rooms\",\"rooms\":" + rarr + "}");

    } else if (type == "message") {
        if (!cl->handshaked || cl->username.empty()) return;
        std::string text = json_get(raw, "text");
        if (text.empty() || text.size() > 2000) return;

        std::string json = "{\"type\":\"message\","
                           "\"username\":\"" + json_escape(cl->username) + "\","
                           "\"text\":\"" + json_escape(text) + "\","
                           "\"time\":\"" + timestamp() + "\","
                           "\"room\":\"" + json_escape(cl->room) + "\"}";
        push_history(cl->room, json);
        broadcast(json, cl->room);
        std::cout << "[" << cl->room << "] " << cl->username << ": " << text << "\n";

    } else if (type == "typing") {
        if (cl->username.empty()) return;
        std::string json = "{\"type\":\"typing\","
                           "\"username\":\"" + json_escape(cl->username) + "\","
                           "\"room\":\"" + json_escape(cl->room) + "\"}";
        broadcast(json, cl->room, cl->fd);
    }
}

// ── Per-client thread ────────────────────────────────────────
void client_thread(int fd) {
    auto cl = std::make_shared<Client>(fd);
    {
        std::lock_guard<std::mutex> lk(clients_mtx);
        clients[fd] = cl;
    }

    std::string buf;
    if (!do_handshake(fd, buf)) {
        std::lock_guard<std::mutex> lk(clients_mtx);
        clients.erase(fd);
        return;
    }
    cl->handshaked = true;
    std::cout << "Client connected: fd=" << fd << "\n";

    // Send welcome
    cl->send("{\"type\":\"welcome\",\"msg\":\"Connected to C++ Chat Server\"}");

    while (running) {
        // Poll for data
        struct pollfd pfd = { fd, POLLIN, 0 };
        int r = poll(&pfd, 1, 5000);
        if (r < 0) break;
        if (r == 0) {
            // Send ping
            send_ws_frame(fd, "", 0x09);
            continue;
        }
        char tmp[4096];
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        cl->recv_buf.append(tmp, n);

        while (true) {
            std::string payload;
            bool should_close = false;
            if (!parse_ws_frame(cl->recv_buf, payload, should_close)) break;
            if (should_close) goto disconnect;
            if (!payload.empty()) handle_message(cl, payload);
        }
    }

disconnect:
    std::cout << "Client disconnected: fd=" << fd << "\n";
    std::string room = cl->room;
    std::string user = cl->username;
    {
        std::lock_guard<std::mutex> lk(clients_mtx);
        clients.erase(fd);
    }
    if (!room.empty() && !user.empty()) {
        std::string sys = "{\"type\":\"system\","
                          "\"text\":\"" + json_escape(user) + " left the chat\","
                          "\"time\":\"" + timestamp() + "\","
                          "\"room\":\"" + json_escape(room) + "\"}";
        broadcast(sys, room);
        send_user_list(room);
    }
}

// ── Main ─────────────────────────────────────────────────────
int main() {
    signal(SIGPIPE, SIG_IGN);

    // Read PORT from environment (Railway sets this automatically)
    int PORT = 8080;
    const char* port_env = std::getenv("PORT");
    if (port_env && strlen(port_env) > 0) {
        try { PORT = std::stoi(port_env); } catch(...) { PORT = 8080; }
    }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv, BACKLOG) < 0) { perror("listen"); return 1; }

    std::cout << "NexusChat server running on port " << PORT << std::endl;
    std::cerr << "NexusChat server running on port " << PORT << std::endl;

    // Pre-create general room history
    room_history["general"];
    room_history["tech"];
    room_history["random"];

    while (running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        struct pollfd pfd = { srv, POLLIN, 0 };
        if (poll(&pfd, 1, 1000) <= 0) continue;

        int client_fd = accept(srv, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        std::thread(client_thread, client_fd).detach();
    }

    close(srv);
    return 0;
}
