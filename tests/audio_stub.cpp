#include "audio.h"

void audioBegin() {}
void sfxPlay(uint8_t) {}
void audioSetEnabled(bool) {}
bool audioEnabled() { return true; }
void audioSetMode(uint8_t) {}
uint8_t audioMode() { return SOUND_FULL; }
void audioSetVolume(uint8_t) {}
uint8_t audioVolume() { return 75; }
bool audioBusy() { return false; }
