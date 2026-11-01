// Settings (stored in /data/jellyfin/settings.json): playback, subtitles,
// languages (per user), autoplay and audio output. Up/Down choose a row,
// Left/Right (or X) change its value; changes are saved immediately.
#pragma once

#include "screen.h"

#include <string>
#include <vector>

class SettingsScreen : public Screen {
public:
	SettingsScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	struct Option {
		std::string label;
		std::string value; // stored value
	};
	struct Row {
		std::string title, help;
		std::string key;
		bool per_user = false;
		bool is_int = false, is_bool = false;
		std::vector<Option> options;
		int current = 0;
	};
	void build();
	void change(Row &row, int delta);

	Fonts &fonts_;
	Navigator &nav_;
	std::vector<Row> rows_;
	int row_ = 0;
	float focus_[16] = {};
	float scroll_ = 0;
};
