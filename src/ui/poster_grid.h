// A scrolling grid of poster cards (movies, series, episodes, collections,
// genres). D-pad moves; L2 / R2 jump to the previous / next letter when
// the list is sorted by name, else a page up / down.
#pragma once

#include "../core/jellyfin.h"
#include "components.h"
#include "screen.h"

#include <string>
#include <vector>

class PosterGrid {
public:
	enum Action { NONE, OPEN, LEAVE_LEFT, LEAVE_UP };

	// Keeps the selection on the same item id if it's still there.
	void set_items(const std::vector<jf::Item> &items);
	const std::vector<jf::Item> &items() const { return items_; }
	const jf::Item *current() const;
	int selected() const { return selected_; }
	void set_focused(bool f) { focused_ = f; }
	bool focused() const { return focused_; }
	void set_letter_jump(bool by_name) { by_name_ = by_name; }

	Action update(double dt, uint32_t pressed);
	// Cards from (x, top) in `cols` columns; rows above `clip_top` are hidden.
	void draw(Renderer &r, Fonts &fonts, float x, float top, float clip_top, float bottom);

	static const int COLS = 6;

private:
	std::vector<jf::Item> items_;
	std::vector<float> focus_;
	int selected_ = 0;
	float scroll_ = 0;
	bool focused_ = false;
	bool by_name_ = true;
	std::string letter_;   // shown briefly after a letter jump
	float letter_time_ = 0;
	float view_h_ = 850;
};

// Upper-case first letter of a sort name ('#' for digits and symbols).
std::string index_letter(const jf::Item &item);
