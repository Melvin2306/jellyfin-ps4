#include "tracks.h"

#include "settings.h"

#include <cctype>

namespace tracks {

namespace {

// Lower case, with ISO 639-2 terminology codes mapped to the bibliographic
// ones Jellyfin mostly uses (deu -> ger, fra -> fre...), and 639-1 codes.
std::string lower(std::string s) {
	for (char &c : s) c = (char)tolower((unsigned char)c);
	static const char *same[][2] = { { "deu", "ger" }, { "de", "ger" }, { "fra", "fre" }, { "fr", "fre" }, { "nld", "dut" },
		{ "nl", "dut" }, { "zho", "chi" }, { "zh", "chi" }, { "ces", "cze" }, { "en", "eng" }, { "es", "spa" }, { "it", "ita" },
		{ "pt", "por" }, { "ru", "rus" }, { "ja", "jpn" }, { "ko", "kor" }, { "pl", "pol" }, { "tr", "tur" }, { "sv", "swe" },
		{ "da", "dan" }, { "no", "nor" }, { "nob", "nor" }, { "fi", "fin" }, { "ar", "ara" }, { "hi", "hin" } };
	for (auto &p : same)
		if (s == p[0]) return p[1];
	return s;
}

bool image_codec(const std::string &codec) {
	const std::string c = lower(codec);
	return c == "pgssub" || c == "hdmv_pgs_subtitle" || c == "dvdsub" || c == "dvd_subtitle" || c == "vobsub" || c == "pgs";
}

} // namespace

bool subtitle_supported(const jf::Stream &s, bool transcode) {
	if (s.is_text) return true;
	return !transcode && !s.is_external && image_codec(s.codec);
}

int default_audio(const jf::Item &item) {
	const std::vector<jf::Stream> audio = item.audio_streams();
	if (audio.empty()) return -1;
	const std::string lang = settings::user_string(jf::client().user_id(), "audio_language");
	if (!lang.empty()) {
		for (const jf::Stream &s : audio)
			if (lower(s.language) == lower(lang)) return s.index;
	}
	for (const jf::Stream &s : audio)
		if (s.is_default) return s.index;
	return audio[0].index;
}

int default_subtitle(const jf::Item &item, bool transcode) {
	const std::string lang = settings::user_string(jf::client().user_id(), "subtitle_language");
	const std::vector<jf::Stream> subs = item.subtitle_streams();
	if (lang == "off") return -1;
	if (!lang.empty()) {
		// The language's full subtitles first, then its forced ones.
		for (int pass = 0; pass < 2; pass++) {
			for (const jf::Stream &s : subs) {
				if (lower(s.language) == lower(lang) && s.is_forced == (pass == 1) && subtitle_supported(s, transcode)) {
					return s.index;
				}
			}
		}
		return -1;
	}
	// No preference: what the file marks as default or forced.
	for (const jf::Stream &s : subs)
		if ((s.is_default || s.is_forced) && subtitle_supported(s, transcode)) return s.index;
	return -1;
}

void remember_audio(const jf::Item &item, int index) {
	const jf::Stream *s = item.stream(index);
	if (s && !s->language.empty()) settings::set_user(jf::client().user_id(), "audio_language", s->language);
}

void remember_subtitle(const jf::Item &item, int index) {
	const jf::Stream *s = item.stream(index);
	if (index < 0) settings::set_user(jf::client().user_id(), "subtitle_language", "off");
	else if (s && !s->language.empty()) settings::set_user(jf::client().user_id(), "subtitle_language", s->language);
}

std::string audio_label(const jf::Item &item, int index) {
	const jf::Stream *s = item.stream(index);
	return s ? s->title : "Default";
}

std::string subtitle_label(const jf::Item &item, int index) {
	if (index < 0) return "Off";
	const jf::Stream *s = item.stream(index);
	return s ? s->title : "Off";
}

} // namespace tracks
