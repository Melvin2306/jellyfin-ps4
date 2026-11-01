// A titled poster grid for any item query (a genre, a collection), with
// sorting (Square). Also: open_item(), which opens the right screen for an
// item (details, collection contents, genre contents).
#pragma once

#include "../core/jellyfin.h"
#include "filter_panel.h"
#include "poster_grid.h"
#include "screen.h"

#include <memory>
#include <string>

namespace sorting {
const int COUNT = 4;
const char *label(int sort);       // "Name", "Year", "Date added", "Rating"
std::string params(int sort);      // SortBy=..&SortOrder=..
} // namespace sorting

void open_item(Fonts &fonts, Navigator &nav, const jf::Item &item);

class BrowseScreen : public Screen {
public:
	// `params`: the query without sorting, e.g. "ParentId=..".
	BrowseScreen(Fonts &fonts, Navigator &nav, const std::string &title, const std::string &params);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	void load();

	Fonts &fonts_;
	Navigator &nav_;
	std::string title_, params_;
	int sort_ = 0;
	Filters filters_;
	FilterPanel filter_panel_;
	PosterGrid grid_;
	bool loading_ = false;
	std::string error_;
	int seen_library_rev_ = 0;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};
