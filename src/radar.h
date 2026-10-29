// Weather radar app: downloads and loop timing; see radar.cpp.
#pragma once
#include <stdint.h>

void radarBegin();                                      // starts the radar task
void radarWant(bool on, float lat, float lon, int z);   // from loop(): what to show (stops 30 s after it isn't on)
void radarStop();                                       // stop now, skipping the 30 s linger (the camera needs the RAM)
bool radarOpen();                                       // it's downloading, or holds its TLS connection
void radarLookup(const char* zip);                      // look a US ZIP code up; the answer comes via radarPump()
bool radarPump();                                       // from loop(): hands frames/answers to ui.cpp; true = redraw
bool radarUp();                                         // the loop was fetched from NOAA in the last 15 min
