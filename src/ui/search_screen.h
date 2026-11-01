// Search: the query is typed with the PS4 system keyboard (built-in
// keyboard as fallback); results (movies, series, episodes) in a grid.
#pragma once

#include "keyboard.h"
#include "poster_grid.h"
#include "screen.h"

#include <memory>
#include <string>

class SearchScreen : public Screen {
public:
	SearchScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	void edit();
	void run();

	Fonts &fonts_;
	Navigator &nav_;
	std::string query_, searched_;
	bool editing_ = false, fallback_kb_ = false;
	Keyboard keyboard_;
	double cooldown_ = 0;
	bool in_grid_ = false;
	float field_focus_ = 1;
	PosterGrid grid_;
	bool loading_ = false;
	std::string error_;
	int generation_ = 0;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
