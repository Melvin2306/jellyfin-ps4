#include "byte_source.h"

#include "range_reader.h"
#include "stream_reader.h"

bool ByteSource::supports(const std::string &url) {
	return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::unique_ptr<ByteSource> ByteSource::create(const std::string &url, const std::vector<std::string> &headers) {
	// Jellyfin transcodes (/Videos/{id}/stream.ts?static=false) have no length
	// and no Range support: read them progressively.
	if (url.find("/stream.ts?") != std::string::npos && url.find("static=false") != std::string::npos) {
		return std::unique_ptr<ByteSource>(new StreamReader(url, headers));
	}
	return std::unique_ptr<ByteSource>(new RangeReader(url, headers));
}
