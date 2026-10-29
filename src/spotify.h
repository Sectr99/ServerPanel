// Spotify app: playback state and controls over the Spotify Web API; see spotify.cpp.
#pragma once
#include <stdint.h>

void spotifyBegin();                  // starts the Spotify task; does nothing with SPOTIFY_REFRESH empty
void spotifyWant(bool on);            // from loop(): poll while the app is on screen (stops 30 s after it isn't)
void spotifyBrowse(bool library, bool devices);  // from loop(): the library or devices sheet is showing
void spotifyStop();                   // stop now, skipping the 30 s linger (another TLS stream needs the RAM)
bool spotifyOpen();                   // it holds its TLS connection
void spotifyCommand(uint8_t cmd, int arg);  // a SpotifyCmd from the screen; shown at once, sent by the task
bool spotifyPump();                   // from loop(): hands state and album art to ui.cpp; true = redraw
bool spotifyUp();                     // Spotify's last answer was a good one
