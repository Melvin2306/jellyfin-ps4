// "Audio & subtitles" panel: two columns (Audio | Subtitles) listing the
// item's streams. Left/Right switch column, Up/Down move, X picks (and
// remembers the language), O or Triangle closes. Unsupported subtitles are
// listed but greyed out.
#pragma once

#include "../core/jellyfin.h"
#include "components.h"
#include "screen.h"

#include <vector>

class TrackMenu {
public:
	enum Result { NONE, CLOSED, AUDIO_CHANGED, SUBTITLE_CHANGED };

	// column: 0 audio, 1 subtitles.
	void open(const jf::Item &item, bool transcode, int audio, int subtitle, int column = 0);
	bool visible() const { return open_; }
	Result update(double dt, uint32_t pressed);
	void draw(Renderer &r, Fonts &fonts);
	int audio() const { return audio_; }
	int subtitle() const { return subtitle_; }

private:
	struct Row {
		int index;          // Jellyfin stream index (-1 = subtitles off)
		std::string label, detail;
		bool supported = true;
	};
	jf::Item item_;
	bool transcode_ = false;
	bool open_ = false;
	int audio_ = -1, subtitle_ = -1;
	int column_ = 0;
	int selected_[2] = {};
	float scroll_[2] = {};
	std::vector<Row> rows_[2];
};
