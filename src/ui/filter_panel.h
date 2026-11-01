// Filters for a library / genre / collection grid: year range, minimum
// community rating, unwatched only. FilterPanel is an overlay (like the
// track menu) that edits a Filters; the owner reloads when it changes.
#pragma once

#include "components.h"
#include "screen.h"

#include <string>

struct Filters {
	int year_from = 0;   // 0 = any
	int year_to = 0;     // 0 = any
	int min_rating = 0;  // 0 = any, else 1..9
	bool unwatched = false;

	bool active() const { return year_from || year_to || min_rating || unwatched; }
	// Query parameters to append to an /Items request ("" when inactive).
	std::string query() const;
	// "2000-2010 · 7+ · Unwatched" for the header, or "".
	std::string summary() const;
};

class FilterPanel {
public:
	void open(const Filters &f) {
		filters_ = f;
		open_ = true;
		row_ = 0;
	}
	bool visible() const { return open_; }
	// Returns true when the filters changed this frame.
	bool update(double dt, uint32_t pressed);
	void draw(Renderer &r, Fonts &fonts);
	const Filters &filters() const { return filters_; }

private:
	Filters filters_;
	bool open_ = false;
	int row_ = 0;
	float focus_[8] = {};
};
