// Audio / subtitle choice: what's playable, the default pick from the user's
// remembered languages, and labels for the menus.
#pragma once

#include "jellyfin.h"

#include <string>

namespace tracks {

// Can this subtitle be shown? Text: always (embedded ones are decoded by
// FFmpeg in direct play, otherwise fetched as SRT from the server). Image
// (PGS / VobSub): only embedded in direct play, decoded by FFmpeg; never via
// the server, which could only burn them in (too heavy for small servers).
bool subtitle_supported(const jf::Stream &s, bool transcode);

// Default picks for `item` from the remembered languages ("" = the file's
// default track, "off" = no subtitles). Returns Jellyfin stream indexes;
// subtitle -1 = off.
int default_audio(const jf::Item &item);
int default_subtitle(const jf::Item &item, bool transcode);

// Remembers the language of a manual choice as the user's default.
void remember_audio(const jf::Item &item, int index);
void remember_subtitle(const jf::Item &item, int index);

std::string audio_label(const jf::Item &item, int index);
std::string subtitle_label(const jf::Item &item, int index);

} // namespace tracks
