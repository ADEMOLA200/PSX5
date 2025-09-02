#include "sdl_audio.h"
#ifdef ENABLE_SDL_AUDIO
#include <SDL2/SDL.h>
#include <cmath>
#include <thread>
#include <atomic>
#include <iostream>
struct SDLAudio::Impl { SDL_AudioDeviceID dev; std::atomic<bool> running; Impl():dev(0),running(false){} };
SDLAudio::SDLAudio():impl_(new Impl()){}
SDLAudio::~SDLAudio(){ delete impl_; }

bool SDLAudio::init(){ if(SDL_Init(SDL_INIT_AUDIO)!=0){ std::cerr<<"SDL_Init failed"<<SDL_GetError()<<std::endl; return false;} SDL_AudioSpec want; SDL_zero(want); want.freq=48000; want.format=AUDIO_F32SYS; want.channels=2; want.samples=1024; want.callback=nullptr; impl_->dev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0); if(!impl_->dev){ std::cerr<<"SDL_OpenAudioDevice failed"<<SDL_GetError()<<std::endl; return false;} SDL_PauseAudioDevice(impl_->dev, 0); impl_->running=true; return true; }
void SDLAudio::shutdown(){ if(impl_->dev){ SDL_CloseAudioDevice(impl_->dev); impl_->dev=0; } SDL_QuitSubSystem(SDL_INIT_AUDIO); }
void SDLAudio::play_tone(float frequency, float duration){ if(!impl_->dev) return; int sampleRate=48000; int total = int(duration*sampleRate); std::vector<float> buf(total*2); for(int i=0;i<total;i++){ float v = sinf(2.0f*M_PI*frequency*(i/(float)sampleRate)); buf[i*2]=v; buf[i*2+1]=v; } SDL_QueueAudio(impl_->dev, buf.data(), buf.size()*sizeof(float)); }
#endif
