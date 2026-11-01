// On-screen keyboard for the DualShock 4: D-pad moves, X types, Square
// deletes, Triangle toggles upper case, Options / "Done" / Circle closes.
#pragma once

#include "screen.h"

#include <string>
#include <vector>

class Keyboard {
public:
	enum Result { NONE, DONE };

	Keyboard();
	// Edits `text` with this frame's presses. DONE when the user closes it.
	Result update(double dt, uint32_t pressed, std::string &text, size_t max_len = 200);
	void draw(Renderer &r, Fonts &fonts, float x, float y);
	float width() const;
	float height() const;

private:
	struct Key {
		std::string label, text; // text: what's typed ("" for special keys)
		int col, row, span;
		float focus = 0;
	};
	int key_at(int col, int row) const;
	std::string label_of(const Key &k) const;

	std::vector<Key> keys_;
	int key_ = 0;
	bool upper_ = false;
};
