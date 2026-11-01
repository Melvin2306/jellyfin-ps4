// Idle screensaver: slowly cross-fading backdrop art with the title and a
// line of the synopsis, to avoid OLED burn-in. Any button dismisses it.
#pragma once

#include "../core/jellyfin.h"
#include "screen.h"

#include <memory>
#include <string>
#include <vector>

class Screensaver {
public:
	void activate();   // loads art (once) and starts showing
	void deactivate();
	bool active() const { return active_; }
	void update(double dt);
	void draw(Renderer &r, Fonts &fonts);

private:
	bool active_ = false;
	bool loading_ = false;
	std::vector<jf::Item> items_;
	int index_ = 0;
	double hold_ = 0;  // seconds the current backdrop has shown
	float fade_ = 0;   // 0..1 fade-in of the whole saver and of each image
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
