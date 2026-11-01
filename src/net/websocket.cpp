#include "websocket.h"

#include "../platform/platform.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>

#if defined(STREMIO_PS4) || defined(JELLYFIN_PS4)
#include <orbis/Net.h>
#endif

namespace ws {

namespace {

const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const uint8_t *in, size_t n) {
	std::string out;
	for (size_t i = 0; i < n; i += 3) {
		const uint32_t b0 = in[i];
		const uint32_t b1 = i + 1 < n ? in[i + 1] : 0;
		const uint32_t b2 = i + 2 < n ? in[i + 2] : 0;
		const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
		out += B64[(v >> 18) & 63];
		out += B64[(v >> 12) & 63];
		out += i + 1 < n ? B64[(v >> 6) & 63] : '=';
		out += i + 2 < n ? B64[v & 63] : '=';
	}
	return out;
}

uint32_t rng() {
	static uint64_t s = 0x2545F4914F6CDD1DULL ^ (uint64_t)(platform::now() * 1e6);
	s ^= s << 13;
	s ^= s >> 7;
	s ^= s << 17;
	return (uint32_t)(s >> 32);
}

bool resolve(const std::string &host, int port, sockaddr_in &addr) {
	memset(&addr, 0, sizeof(addr));
#if defined(STREMIO_PS4) || defined(JELLYFIN_PS4)
	addr.sin_len = sizeof(addr);
#endif
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	// Numeric IP (the common LAN case).
	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) return true;
#if defined(STREMIO_PS4) || defined(JELLYFIN_PS4)
	// Resolve a hostname via sceNetResolver.
	int rid = sceNetResolverCreate("jf_ws", 0, 0);
	if (rid < 0) return false;
	OrbisNetInAddr out;
	const int rc = sceNetResolverStartNtoa(rid, host.c_str(), &out, 0, 0, 0);
	sceNetResolverDestroy(rid);
	if (rc < 0) return false;
	addr.sin_addr.s_addr = out.s_addr;
	return true;
#else
	return false;
#endif
}

} // namespace

Socket::~Socket() {
	close();
}

bool Socket::read_all(uint8_t *buf, size_t n) {
	size_t got = 0;
	while (got < n) {
		const long r = recv(fd_, buf + got, n - got, 0);
		if (r <= 0) return false;
		got += (size_t)r;
	}
	return true;
}

bool Socket::connect(const std::string &host, int port, const std::string &path, std::string &error) {
	sockaddr_in addr;
	if (!resolve(host, port, addr)) {
		error = "couldn't resolve " + host;
		return false;
	}
	fd_ = socket(AF_INET, SOCK_STREAM, 0);
	if (fd_ < 0) {
		error = "socket failed";
		return false;
	}
	// A read timeout lets the reader loop wake to send keep-alives and check
	// the stop flag.
	timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (::connect(fd_, (sockaddr *)&addr, sizeof(addr)) != 0) {
		error = "connect failed";
		close();
		return false;
	}
	uint8_t key[16];
	for (int i = 0; i < 16; i++) key[i] = (uint8_t)rng();
	const std::string wskey = base64(key, 16);
	std::string req = "GET " + path + " HTTP/1.1\r\n";
	req += "Host: " + host + "\r\n";
	req += "Upgrade: websocket\r\n";
	req += "Connection: Upgrade\r\n";
	req += "Sec-WebSocket-Key: " + wskey + "\r\n";
	req += "Sec-WebSocket-Version: 13\r\n\r\n";
	if (send(fd_, req.data(), req.size(), 0) != (long)req.size()) {
		error = "handshake send failed";
		close();
		return false;
	}
	// Read the response headers up to the blank line.
	std::string resp;
	char c;
	while (resp.find("\r\n\r\n") == std::string::npos && resp.size() < 4096) {
		const long r = recv(fd_, &c, 1, 0);
		if (r <= 0) {
			error = "handshake no response";
			close();
			return false;
		}
		resp += c;
	}
	if (resp.find(" 101") == std::string::npos) {
		error = "server did not upgrade: " + resp.substr(0, resp.find("\r\n"));
		close();
		return false;
	}
	platform::log("ws: connected to %s:%d%s", host.c_str(), port, path.substr(0, path.find('?')).c_str());
	return true;
}

bool Socket::send_text(const std::string &text) {
	if (fd_ < 0) return false;
	std::string frame;
	frame += (char)0x81; // FIN + text
	const size_t n = text.size();
	uint8_t mask[4];
	for (int i = 0; i < 4; i++) mask[i] = (uint8_t)rng();
	if (n < 126) {
		frame += (char)(0x80 | n);
	} else if (n < 65536) {
		frame += (char)(0x80 | 126);
		frame += (char)((n >> 8) & 0xFF);
		frame += (char)(n & 0xFF);
	} else {
		frame += (char)(0x80 | 127);
		for (int i = 7; i >= 0; i--) frame += (char)((n >> (i * 8)) & 0xFF);
	}
	frame.append((char *)mask, 4);
	for (size_t i = 0; i < n; i++) frame += (char)(text[i] ^ mask[i & 3]);
	return send(fd_, frame.data(), frame.size(), 0) == (long)frame.size();
}

int Socket::recv_text(std::string &out) {
	if (fd_ < 0) return -1;
	uint8_t hdr[2];
	const long r = recv(fd_, hdr, 2, 0);
	if (r == 0) return -1;
	if (r < 0) return 0; // timeout (SO_RCVTIMEO) or interrupted: nothing yet
	if (r < 2 && !read_all(hdr + r, 2 - r)) return -1;
	const int opcode = hdr[0] & 0x0F;
	const bool masked = (hdr[1] & 0x80) != 0; // server frames are not masked
	uint64_t len = hdr[1] & 0x7F;
	if (len == 126) {
		uint8_t e[2];
		if (!read_all(e, 2)) return -1;
		len = ((uint64_t)e[0] << 8) | e[1];
	} else if (len == 127) {
		uint8_t e[8];
		if (!read_all(e, 8)) return -1;
		len = 0;
		for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
	}
	uint8_t mask[4] = { 0, 0, 0, 0 };
	if (masked && !read_all(mask, 4)) return -1;
	if (len > 1 << 20) return -1; // sane cap
	std::string payload;
	payload.resize((size_t)len);
	if (len > 0 && !read_all((uint8_t *)&payload[0], (size_t)len)) return -1;
	if (masked)
		for (size_t i = 0; i < payload.size(); i++) payload[i] ^= mask[i & 3];
	switch (opcode) {
		case 0x1: // text
			out = payload;
			return 1;
		case 0x8: // close
			return -1;
		case 0x9: { // ping -> pong
			std::string pong;
			pong += (char)0x8A;
			pong += (char)(0x80 | payload.size());
			uint8_t m[4];
			for (int i = 0; i < 4; i++) m[i] = (uint8_t)rng();
			pong.append((char *)m, 4);
			for (size_t i = 0; i < payload.size(); i++) pong += (char)(payload[i] ^ m[i & 3]);
			send(fd_, pong.data(), pong.size(), 0);
			return 0;
		}
		default: return 0; // pong, continuation: ignore
	}
}

void Socket::close() {
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
	}
}

} // namespace ws
