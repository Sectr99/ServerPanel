// Stream Deck surface for Bitfocus Companion over its Satellite API; see companion.cpp.
#pragma once

void companionBegin();         // starts the connection task; does nothing with COMPANION_HOST empty
bool companionPump();          // from loop(): hands newly arrived key images to ui.cpp; true if any did
void companionPress(int key);  // a deck key was tapped
bool companionUp();            // Companion has accepted the surface
