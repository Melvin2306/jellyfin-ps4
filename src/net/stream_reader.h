// ByteSource over a progressive HTTP stream of unknown length (a Jellyfin
// transcode). A download thread (1 MiB stack, libSceHttp needs it) reads
// ahead into memory; reads may go back a few MB (FFmpeg's probing), but not
// further: seeking is done by restarting the transcode at a new position.
#pragma once

#include "../platform/threads.h"
#include "byte_source.h"
#include "http.h"

#include <atomic>
#include <string>
#include <vector>

class StreamReader : public ByteSource {
public:
	StreamReader(const std::string &url, const std::vector<std::string> &headers);
	~StreamReader() override;

	bool open() override;
	int read(uint64_t pos, uint8_t *buf, int len) override;
	uint64_t size() const override { return 0; } // unknown
	void abort() override;
	std::string error() override;
	std::string status() override;

private:
	static void *thread_entry(void *self);
	void download();

	std::string url_;
	std::vector<std::string> headers_;
	threads::Mutex mutex_;
	threads::CondVar cond_;
	std::vector<uint8_t> data_; // bytes [base_, base_ + data_.size())
	uint64_t base_ = 0;
	uint64_t read_pos_ = 0; // furthest position read so far
	bool opened_ = false, eof_ = false, failed_ = false;
	std::string error_;
	std::atomic<bool> abort_{ false };
	std::atomic<bool> running_{ false };
	double started_ = 0;
};
