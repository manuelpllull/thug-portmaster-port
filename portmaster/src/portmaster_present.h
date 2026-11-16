#ifndef PORTMASTER_PRESENT_H
#define PORTMASTER_PRESENT_H

#include <SDL2/SDL.h>

// Presenter thread (see portmaster_present.cpp). init runs once on the game
// thread with the GL context current; false = old SwapWindow path.
bool thug_present_init(SDL_Window *window, SDL_GLContext ctx1, int w, int h);
bool thug_present_active();
// Before init: the panel's refresh rate (steady pacing needs it).
void thug_present_set_refresh(int hz);
// Steady pacing (THUG_PACING_MODE=steady): the game time step in us, else 0.
unsigned thug_present_step_us();
// Quitting: show what is queued, end the presenter thread. No-op when it is off.
void thug_present_stop();
// End of a frame: hand the off-screen buffer to the presenter, switch to the
// next one (waits only if both are still in flight).
void thug_present_submit(float cpu_ms);
// FPS meter drawn by the presenter (L3 + Select).
void thug_present_toggle_overlay();
bool thug_present_overlay();

#endif
