#pragma once
#ifdef ENABLE_SDL_AUDIO
#include <cstdint>
class SDLAudio {
public:
    SDLAudio();
    ~SDLAudio();
    bool init();
    void shutdown();
    void play_tone(float frequency, float duration);
private:
    struct Impl; Impl* impl_;
};
#endif
