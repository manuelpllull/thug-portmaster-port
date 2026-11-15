#ifndef PORTMASTER_GLREC_H
#define PORTMASTER_GLREC_H

// Render thread (THUG_RENDER_THREAD, see portmaster_glrec.cpp).

// GL entry point lookup handed to gl4es (set_getprocaddress): returns the
// recording stub of a GLES function, resolving the real one on the way.
extern "C" void *thug_glrec_getproc(const char *name);
// Game thread, after the presenter thread is on and the game context is
// surfaceless and current here: move the context to a render thread and start
// recording. false = stays on the game thread (old path).
bool thug_glrec_start();
bool thug_glrec_active();
// Quitting: run what is recorded, end the render thread. No-op when it is off.
void thug_glrec_stop();
// End of frame (game thread): records the present (run by the render thread
// through thug_present_submit_rt) and waits if the render thread is more than
// one frame behind.
void thug_glrec_present(float cpu_ms, int mirror);

#endif
