// Minimal WebSocket client (RFC 6455) over a plain TCP socket, enough to
// receive Jellyfin's session messages. No TLS (the server is plain HTTP).
// Blocking, meant to run on its own thread.
#pragma once

#include <cstdint>
#include <string>

namespace ws {

class Socket {
public:
	~Socket();
	// Connects to host:port and performs the Upgrade handshake for `path`
	// (e.g. "/socket?api_key=..."). False on failure.
	bool connect(const std::string &host, int port, const std::string &path, std::string &error);
	// Sends one text frame (masked, as a client must). False on error.
	bool send_text(const std::string &text);
	// Receives the next text message into `out`. Returns 1 on a message, 0 on
	// timeout (nothing within the socket timeout), -1 on close/error. Control
	// frames (ping/close) are handled internally.
	int recv_text(std::string &out);
	void close();
	bool open() const { return fd_ >= 0; }

private:
	bool read_all(uint8_t *buf, size_t n);
	int fd_ = -1;
};

} // namespace ws
