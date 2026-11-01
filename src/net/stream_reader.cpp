#include "stream_reader.h"

#include "../platform/platform.h"
#include "urls.h"

#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace {
const size_t MAX_AHEAD = 48u * 1024 * 1024;  // stop downloading this far ahead of the reader
const size_t KEEP_BEHIND = 8u * 1024 * 1024; // kept behind the reader (probing re-reads)
const size_t CHUNK = 64 * 1024;
const size_t THREAD_STACK = 1024 * 1024;
} // namespace

StreamReader::StreamReader(const std::string &url, const std::vector<std::string> &headers) : url_(url), headers_(headers) {}

StreamReader::~StreamReader() {
	abort();
	while (running_) usleep(2000);
}

bool StreamReader::open() {
	started_ = platform::now();
	running_ = true;
	if (!threads::start(thread_entry, this, THREAD_STACK, "jf_stream")) {
		running_ = false;
		error_ = "Couldn't start the download";
		return false;
	}
	threads::Lock lock(mutex_);
	while (!opened_ && !failed_ && !abort_) cond_.wait(mutex_);
	return opened_ && !abort_;
}

void *StreamReader::thread_entry(void *self) {
	StreamReader *r = static_cast<StreamReader *>(self);
	r->download();
	r->running_ = false;
	return nullptr;
}

void StreamReader::download() {
	std::unique_ptr<http::StreamConnection> conn = http::open_stream(url_, headers_);
	std::string err;
	const int status = conn->open(err);
	platform::log("stream: %s -> HTTP %d after %.1f s", urls::for_log(url_).c_str(), status, platform::now() - started_);
	{
		threads::Lock lock(mutex_);
		if (status < 200 || status >= 300) {
			failed_ = true;
			char buf[64];
			snprintf(buf, sizeof(buf), "Server returned HTTP %d", status);
			error_ = status < 0 ? "Transcode failed: " + err : buf;
			cond_.notify_all();
			return;
		}
		opened_ = true;
		cond_.notify_all();
	}
	std::vector<uint8_t> chunk(CHUNK);
	uint64_t total = 0;
	while (!abort_) {
		{
			threads::Lock lock(mutex_);
			while (!abort_ && base_ + data_.size() > read_pos_ + MAX_AHEAD) cond_.wait(mutex_);
			if (abort_) break;
		}
		const int n = conn->read(chunk.data(), (int)chunk.size());
		threads::Lock lock(mutex_);
		if (n < 0) {
			char buf[64];
			snprintf(buf, sizeof(buf), "Stream error 0x%08X", (uint32_t)n);
			error_ = buf;
			failed_ = true;
			cond_.notify_all();
			break;
		}
		if (n == 0) {
			eof_ = true;
			cond_.notify_all();
			platform::log("stream: end after %llu bytes", (unsigned long long)total);
			break;
		}
		total += (uint64_t)n;
		data_.insert(data_.end(), chunk.data(), chunk.data() + n);
		// Drop what's far behind the reader.
		if (read_pos_ > base_ + 2 * KEEP_BEHIND) {
			const size_t drop = (size_t)(read_pos_ - base_ - KEEP_BEHIND);
			data_.erase(data_.begin(), data_.begin() + drop);
			base_ += drop;
		}
		cond_.notify_all();
	}
}

int StreamReader::read(uint64_t pos, uint8_t *buf, int len) {
	threads::Lock lock(mutex_);
	for (;;) {
		if (abort_) return -1;
		if (pos < base_) {
			error_ = "Can't seek back in a live transcode";
			return -1;
		}
		if (pos < base_ + data_.size()) break;
		if (eof_) return 0;
		if (failed_) return -1;
		cond_.wait(mutex_);
	}
	const size_t off = (size_t)(pos - base_);
	const int n = (int)std::min<size_t>((size_t)len, data_.size() - off);
	memcpy(buf, data_.data() + off, (size_t)n);
	if (pos + n > read_pos_) {
		read_pos_ = pos + n;
		cond_.notify_all(); // the downloader may continue
	}
	return n;
}

void StreamReader::abort() {
	abort_ = true;
	threads::Lock lock(mutex_);
	cond_.notify_all();
}

std::string StreamReader::error() {
	threads::Lock lock(mutex_);
	return error_;
}

std::string StreamReader::status() {
	threads::Lock lock(mutex_);
	if (!opened_) return "Waiting for the server to start transcoding\xE2\x80\xA6";
	return std::string();
}
