#include "browse_screen.h"

#include "../platform/platform.h"
#include "detail_screen.h"
#include "theme.h"

namespace sorting {

const char *label(int sort) {
	static const char *labels[] = { "Name", "Year", "Date added", "Rating" };
	return labels[(sort % COUNT + COUNT) % COUNT];
}

std::string params(int sort) {
	switch ((sort % COUNT + COUNT) % COUNT) {
		case 1: return "SortBy=ProductionYear,SortName&SortOrder=Descending";
		case 2: return "SortBy=DateCreated,SortName&SortOrder=Descending";
		case 3: return "SortBy=CommunityRating,SortName&SortOrder=Descending";
		default: return "SortBy=SortName&SortOrder=Ascending";
	}
}

} // namespace sorting

void open_item(Fonts &fonts, Navigator &nav, const jf::Item &item) {
	platform::log("open %s", item.type.c_str());
	if (item.type == "BoxSet") {
		nav.push(std::unique_ptr<Screen>(new BrowseScreen(fonts, nav, item.name, "ParentId=" + item.id)));
	} else if (item.type == "Genre") {
		nav.push(std::unique_ptr<Screen>(new BrowseScreen(fonts, nav, item.name,
				"GenreIds=" + item.id + "&IncludeItemTypes=Movie,Series&Recursive=true")));
	} else {
		nav.push(std::unique_ptr<Screen>(new DetailScreen(fonts, nav, item)));
	}
}

BrowseScreen::BrowseScreen(Fonts &fonts, Navigator &nav, const std::string &title, const std::string &params)
	: fonts_(fonts), nav_(nav), title_(title), params_(params) {
	seen_library_rev_ = jf::client().library_rev;
	grid_.set_focused(true);
	load();
}

void BrowseScreen::load() {
	loading_ = true;
	std::weak_ptr<int> alive = alive_;
	jf::client().browse(params_ + "&" + sorting::params(sort_) + "&Limit=1000",
			[this, alive](bool ok, const std::string &error, const std::vector<jf::Item> &items) {
				if (alive.expired()) return;
				loading_ = false;
				error_ = ok ? "" : error;
				if (ok) grid_.set_items(items);
			});
}

void BrowseScreen::update(double dt, uint32_t pressed) {
	if (jf::client().library_rev != seen_library_rev_) {
		seen_library_rev_ = jf::client().library_rev;
		load();
	}
	if (pressed & platform::BUTTON_BACK) {
		nav_.pop();
		return;
	}
	if (pressed & platform::BUTTON_SQUARE) {
		sort_ = (sort_ + 1) % sorting::COUNT;
		grid_.set_letter_jump(sort_ == 0);
		load();
	}
	if (grid_.update(dt, pressed) == PosterGrid::OPEN && grid_.current()) open_item(fonts_, nav_, *grid_.current());
}

void BrowseScreen::draw(Renderer &r) {
	grid_.draw(r, fonts_, 120, 160, 130, 1000);
	r.rect(0, 0, 1920, 125, theme::BASE_100);
	r.text(fonts_.section, title_, 120, 95, theme::BASE_CONTENT);
	const std::string sort = std::string("Sort: ") + sorting::label(sort_);
	r.text(fonts_.small, sort, 1800 - fonts_.small.measure(sort), 95, theme::BASE_CONTENT_MUTED);
	if (grid_.items().empty()) {
		const std::string msg = loading_ ? "Loading\xE2\x80\xA6" : !error_.empty() ? "Couldn't load: " + error_ : "Nothing here.";
		r.text(fonts_.body, msg, 120, 220, error_.empty() ? theme::BASE_CONTENT_MUTED : theme::ERROR);
	}
	r.rect(0, 1005, 1920, 75, theme::BASE_100);
	ui::hints(r, fonts_.small, { { "\xC3\x97", "Open" }, { "\xE2\x96\xA1", "Sort" }, { "L2 R2", sort_ == 0 ? "A\xE2\x80\x93Z" : "Page" },
			{ "\xE2\x97\x8B", "Back" } }, 120, 1050);
}
