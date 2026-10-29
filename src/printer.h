// Bambu printer status over its LAN MQTT broker, plus its camera stream; see printer.cpp.
#pragma once
#include "ui.h"

void printerBegin();          // starts the connection tasks; does nothing with PRINTER_IP empty
void printerPause(bool on);   // from loop(): drop the MQTT connection while on (its TLS RAM goes to the radar)
void printerRead(Printer& p); // from loop(): the latest state
bool printerUp();             // connected and at least one report received
uint32_t printerLastMs();     // millis() of the last report; 0 = never
void printerCamWant(bool on); // from loop(): stream the camera while it's on screen (stops 30 s after it isn't)
void printerCamStop();        // stop now, skipping the 30 s linger (another TLS stream needs the RAM)
bool printerCamOpen();        // its TLS connection is open
bool printerCamPump();        // from loop(): hands a new frame to ui.cpp; true if it's worth a redraw
bool printerCamUp();          // a frame arrived in the last 5 s
