#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "winmm.lib")
#elif defined(__linux__)
#include <alsa/asoundlib.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#endif


#ifdef _WIN32
class DirectSoundDevice : public AudioDevice {
private:
    LPDIRECTSOUND8 ds_device;
    LPDIRECTSOUNDBUFFER primary_buffer;
    LPDIRECTSOUNDBUFFER secondary_buffer;
    DWORD buffer_size;
    DWORD write_cursor;
    AudioFormat format;

public:
    bool initialize(const AudioFormat& fmt) override {
        format = fmt;
        
        if (FAILED(DirectSoundCreate8(nullptr, &ds_device, nullptr))) {
            return false;
        }
        
        if (FAILED(ds_device->SetCooperativeLevel(GetDesktopWindow(), DSSCL_PRIORITY))) {
            return false;
        }
        
\        DSBUFFERDESC primary_desc = {};
        primary_desc.dwSize = sizeof(DSBUFFERDESC);
        primary_desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
        
        if (FAILED(ds_device->CreateSoundBuffer(&primary_desc, &primary_buffer, nullptr))) {
            return false;
        }
        
        WAVEFORMATEX wave_format = {};
        wave_format.wFormatTag = WAVE_FORMAT_PCM;
        wave_format.nChannels = format.channels;
        wave_format.nSamplesPerSec = format.sample_rate;
        wave_format.wBitsPerSample = format.bits_per_sample;
        wave_format.nBlockAlign = (wave_format.nChannels * wave_format.wBitsPerSample) / 8;
        wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
        
        primary_buffer->SetFormat(&wave_format);
        
        buffer_size = format.sample_rate * format.channels * (format.bits_per_sample / 8) / 4; // 250ms buffer
        
        DSBUFFERDESC secondary_desc = {};
        secondary_desc.dwSize = sizeof(DSBUFFERDESC);
        secondary_desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
        secondary_desc.dwBufferBytes = buffer_size;
        secondary_desc.lpwfxFormat = &wave_format;
        
        return SUCCEEDED(ds_device->CreateSoundBuffer(&secondary_desc, &secondary_buffer, nullptr));
    }
    
    void start() override {
        if (secondary_buffer) {
            secondary_buffer->Play(0, 0, DSBPLAY_LOOPING);
        }
    }
    
    void stop() override {
        if (secondary_buffer) {
            secondary_buffer->Stop();
        }
    }
    
    void write_samples(const float* samples, size_t frame_count) override {
        if (!secondary_buffer) return;
        
        DWORD play_cursor, write_cursor_pos;
        secondary_buffer->GetCurrentPosition(&play_cursor, &write_cursor_pos);
        
        size_t bytes_to_write = frame_count * format.channels * (format.bits_per_sample / 8);
        
        void* ptr1, *ptr2;
        DWORD bytes1, bytes2;
        
        if (SUCCEEDED(secondary_buffer->Lock(write_cursor, bytes_to_write, &ptr1, &bytes1, &ptr2, &bytes2, 0))) {
            if (format.bits_per_sample == 16) {
                int16_t* dst = (int16_t*)ptr1;
                for (size_t i = 0; i < bytes1 / sizeof(int16_t); i++) {
                    dst[i] = (int16_t)(samples[i] * 32767.0f);
                }
                if (ptr2) {
                    dst = (int16_t*)ptr2;
                    size_t offset = bytes1 / sizeof(int16_t);
                    for (size_t i = 0; i < bytes2 / sizeof(int16_t); i++) {
                        dst[i] = (int16_t)(samples[offset + i] * 32767.0f);
                    }
                }
            }
            
            secondary_buffer->Unlock(ptr1, bytes1, ptr2, bytes2);
            write_cursor = (write_cursor + bytes_to_write) % buffer_size;
        }
    }
    
    size_t get_buffer_size() const override { return buffer_size; }
    double get_latency() const override { return 0.25; } // 250ms
    
    ~DirectSoundDevice() {
        if (secondary_buffer) secondary_buffer->Release();
        if (primary_buffer) primary_buffer->Release();
        if (ds_device) ds_device->Release();
    }
};
#endif

#ifdef __linux__
class ALSADevice : public AudioDevice {
    // TODO: ALSA implementation details needs to be added
};
#endif

#ifdef __APPLE__
class CoreAudioDevice : public AudioDevice {
    // TODO: CoreAudio implementation details needs to be added
};
#endif

Audio::Audio() : running(false) {
    // Initialize APU state
    apu_state.master_volume = 1.0f;
    for (int i = 0; i < 8; i++) {
        apu_state.channel_volumes[i] = 1.0f;
        apu_state.channel_muted[i] = false;
    }
    apu_state.reverb_type = 0;
    apu_state.reverb_depth = 0.0f;
    apu_state.surround_enabled = false;
    
    // Initialize effects
    effects.compressor_enabled = false;
    effects.compressor_threshold = -12.0f;
    effects.compressor_ratio = 4.0f;
    effects.limiter_enabled = true;
    effects.limiter_threshold = -0.1f;
    effects.delay_buffer.resize(48000); // 1 second delay buffer at 48kHz
    effects.delay_write_pos = 0;
}

Audio::~Audio() {
    shutdown();
}

bool Audio::initialize(const AudioFormat& format) {
    current_format = format;
    
#ifdef _WIN32
    device = std::make_unique<DirectSoundDevice>();
#elif defined(__linux__)
    device = std::make_unique<ALSADevice>();
#elif defined(__APPLE__)
    device = std::make_unique<CoreAudioDevice>();
#else
    return false;
#endif
    
    if (!device->initialize(format)) {
        return false;
    }
    
    running = true;
    audio_thread = std::thread(&Audio::audio_thread_func, this);
    device->start();
    
    return true;
}

void Audio::shutdown() {
    if (running) {
        running = false;
        buffer_cv.notify_all();
        
        if (audio_thread.joinable()) {
            audio_thread.join();
        }
        
        if (device) {
            device->stop();
            device.reset();
        }
    }
}

void Audio::push_samples(const float* samples, int frame_count) {
    if (!running || !samples || frame_count <= 0) return;
    
    std::lock_guard<std::mutex> lock(buffer_mutex);
    
    AudioBuffer buffer;
    buffer.frames = frame_count;
    buffer.channels = current_format.channels;
    buffer.data.resize(frame_count * current_format.channels);
    buffer.timestamp = 0.0; // Would be set by system clock
    buffer.is_ready = true;
    
    // Copy and process samples
    std::memcpy(buffer.data.data(), samples, frame_count * current_format.channels * sizeof(float));
    
    // Apply APU processing
    process_audio_effects(buffer.data.data(), frame_count);
    apply_3d_audio(buffer.data.data(), frame_count);
    
    buffer_queue.push_back(std::move(buffer));
    buffer_cv.notify_one();
}

void Audio::audio_thread_func() {
    while (running) {
        std::unique_lock<std::mutex> lock(buffer_mutex);
        buffer_cv.wait(lock, [this] { return !buffer_queue.empty() || !running; });
        
        if (!running) break;
        
        if (!buffer_queue.empty()) {
            AudioBuffer buffer = std::move(buffer_queue.front());
            buffer_queue.erase(buffer_queue.begin());
            lock.unlock();
            
            // Send to audio device
            device->write_samples(buffer.data.data(), buffer.frames);
        }
    }
}

void Audio::process_audio_effects(float* samples, size_t frame_count) {
    // Apply master volume
    for (size_t i = 0; i < frame_count * current_format.channels; i++) {
        samples[i] *= apu_state.master_volume;
    }
    
    // Apply compressor
    if (effects.compressor_enabled) {
        float threshold_linear = std::pow(10.0f, effects.compressor_threshold / 20.0f);
        float ratio_inv = 1.0f / effects.compressor_ratio;
        
        for (size_t i = 0; i < frame_count * current_format.channels; i++) {
            float abs_sample = std::abs(samples[i]);
            if (abs_sample > threshold_linear) {
                float excess = abs_sample - threshold_linear;
                float compressed_excess = excess * ratio_inv;
                float sign = samples[i] >= 0 ? 1.0f : -1.0f;
                samples[i] = sign * (threshold_linear + compressed_excess);
            }
        }
    }
    
    // Apply limiter
    if (effects.limiter_enabled) {
        float limit_linear = std::pow(10.0f, effects.limiter_threshold / 20.0f);
        for (size_t i = 0; i < frame_count * current_format.channels; i++) {
            samples[i] = std::max(-limit_linear, std::min(limit_linear, samples[i]));
        }
    }
    
    // Apply reverb (simplified)
    if (apu_state.reverb_depth > 0.0f) {
        apply_room_reverb(samples, frame_count);
    }
}

void Audio::apply_3d_audio(float* samples, size_t frame_count) {
    if (!apu_state.surround_enabled || current_format.channels < 2) return;
    
    for (const auto& source : audio_sources_) {
        if (!source.second.active) continue;
        
        // Calculate 3D position relative to listener
        float dx = source.second.position.x - listener_position_.x;
        float dy = source.second.position.y - listener_position_.y; 
        float dz = source.second.position.z - listener_position_.z;
        
        float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (distance < 0.001f) distance = 0.001f;
        
        // Calculate azimuth and elevation for HRTF
        float azimuth = std::atan2(dx, dz);
        float elevation = std::atan2(dy, std::sqrt(dx*dx + dz*dz));
        
        // Apply distance attenuation
        float attenuation = 1.0f / (1.0f + distance * 0.1f);
        
        // Apply HRTF (Head-Related Transfer Function) for spatial audio
        for (size_t i = 0; i < frame_count; ++i) {
            if (current_format.channels >= 2) {
                float source_sample = source.second.volume * attenuation;
                
                // Simplified HRTF - real implementation would use measured HRTF data
                float left_gain = 0.5f + 0.5f * std::cos(azimuth + 0.5f);
                float right_gain = 0.5f + 0.5f * std::cos(azimuth - 0.5f);
                
                // Apply elevation filtering
                float elevation_factor = std::cos(elevation);
                left_gain *= elevation_factor;
                right_gain *= elevation_factor;
                
                // Add to output with proper delay for ITD (Interaural Time Difference)
                size_t delay_samples = static_cast<size_t>(std::abs(std::sin(azimuth)) * 0.0006f * current_format.sample_rate);
                
                if (i >= delay_samples) {
                    samples[i * current_format.channels] += source_sample * left_gain;
                    samples[i * current_format.channels + 1] += source_sample * right_gain;
                }
            }
        }
    }
    
    // Apply room reverb simulation
    // TODO: Implement room reverb no simulation
    if (apu_state.reverb_depth > 0.0f) {
        apply_room_reverb(samples, frame_count);
    }
}

void Audio::set_master_volume(float volume) {
    apu_state.master_volume = std::max(0.0f, std::min(1.0f, volume));
}

void Audio::set_channel_volume(int channel, float volume) {
    if (channel >= 0 && channel < 8) {
        apu_state.channel_volumes[channel] = std::max(0.0f, std::min(1.0f, volume));
    }
}

void Audio::mute_channel(int channel, bool muted) {
    if (channel >= 0 && channel < 8) {
        apu_state.channel_muted[channel] = muted;
    }
}

void Audio::enable_surround(bool enabled) {
    apu_state.surround_enabled = enabled;
}

void Audio::set_reverb(int type, float depth) {
    apu_state.reverb_type = type;
    apu_state.reverb_depth = std::max(0.0f, std::min(1.0f, depth));
}

void Audio::enable_3d_audio(bool enabled) {
    apu_state.surround_enabled = enabled;
}

void Audio::set_tempest_3d_params(float listener_x, float listener_y, float listener_z,
                                 float forward_x, float forward_y, float forward_z,
                                 float up_x, float up_y, float up_z) {
    listener_position_ = {listener_x, listener_y, listener_z};
    listener_forward_ = {forward_x, forward_y, forward_z};
    listener_up_ = {up_x, up_y, up_z};
    
    // Calculate right vector for 3D orientation
    listener_right_.x = listener_forward_.y * listener_up_.z - listener_forward_.z * listener_up_.y;
    listener_right_.y = listener_forward_.z * listener_up_.x - listener_forward_.x * listener_up_.z;
    listener_right_.z = listener_forward_.x * listener_up_.y - listener_forward_.y * listener_up_.x;
    
    // Normalize vectors
    float forward_len = std::sqrt(listener_forward_.x*listener_forward_.x + listener_forward_.y*listener_forward_.y + listener_forward_.z*listener_forward_.z);
    if (forward_len > 0.001f) {
        listener_forward_.x /= forward_len;
        listener_forward_.y /= forward_len;
        listener_forward_.z /= forward_len;
    }
    
    float right_len = std::sqrt(listener_right_.x*listener_right_.x + listener_right_.y*listener_right_.y + listener_right_.z*listener_right_.z);
    if (right_len > 0.001f) {
        listener_right_.x /= right_len;
        listener_right_.y /= right_len;
        listener_right_.z /= right_len;
    }
}

void Audio::add_audio_source(int source_id, float x, float y, float z, float volume) {
    AudioSource3D source;
    source.position = {x, y, z};
    source.volume = std::max(0.0f, std::min(1.0f, volume));
    source.active = true;
    source.doppler_factor = 1.0f;
    source.last_position = source.position;
    
    audio_sources_[source_id] = source;
}

void Audio::update_audio_source(int source_id, float x, float y, float z) {
    auto it = audio_sources_.find(source_id);
    if (it != audio_sources_.end()) {
        it->second.last_position = it->second.position;
        it->second.position = {x, y, z};
        
        // Calculate Doppler effect
        float dx = it->second.position.x - it->second.last_position.x;
        float dy = it->second.position.y - it->second.last_position.y;
        float dz = it->second.position.z - it->second.last_position.z;
        
        float velocity = std::sqrt(dx*dx + dy*dy + dz*dz) * current_format.sample_rate / 1024.0f; // Approximate velocity
        float sound_speed = 343.0f; // m/s
        
        // Calculate relative velocity towards listener
        float rel_dx = listener_position_.x - it->second.position.x;
        float rel_dy = listener_position_.y - it->second.position.y;
        float rel_dz = listener_position_.z - it->second.position.z;
        float distance = std::sqrt(rel_dx*rel_dx + rel_dy*rel_dy + rel_dz*rel_dz);
        
        if (distance > 0.001f) {
            float radial_velocity = (dx * rel_dx + dy * rel_dy + dz * rel_dz) / distance;
            it->second.doppler_factor = sound_speed / (sound_speed - radial_velocity);
            it->second.doppler_factor = std::max(0.5f, std::min(2.0f, it->second.doppler_factor)); // Clamp for stability
        }
    }
}

void Audio::remove_audio_source(int source_id) {
    auto it = audio_sources_.find(source_id);
    if (it != audio_sources_.end()) {
        it->second.active = false;
        audio_sources_.erase(it);
    }
}

size_t Audio::get_queued_frames() const {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    size_t total_frames = 0;
    for (const auto& buffer : buffer_queue) {
        total_frames += buffer.frames;
    }
    return total_frames;
}

void Audio::apply_room_reverb(float* samples, size_t frame_count) {
    // Real room reverb simulation with multiple delay lines
    // TODO: Implement room reverb
    static std::vector<float> delay_line1(4800, 0.0f); // 100ms
    static std::vector<float> delay_line2(7200, 0.0f); // 150ms  
    static std::vector<float> delay_line3(9600, 0.0f); // 200ms
    static size_t delay_pos1 = 0, delay_pos2 = 0, delay_pos3 = 0;
    
    for (size_t i = 0; i < frame_count * current_format.channels; ++i) {
        float input = samples[i];
        
        float delayed1 = delay_line1[delay_pos1];
        float delayed2 = delay_line2[delay_pos2];
        float delayed3 = delay_line3[delay_pos3];
        
        delay_line1[delay_pos1] = input + delayed1 * 0.3f;
        delay_line2[delay_pos2] = input + delayed2 * 0.25f;
        delay_line3[delay_pos3] = input + delayed3 * 0.2f;
        
        samples[i] += (delayed1 + delayed2 + delayed3) * apu_state.reverb_depth * 0.33f;
        
        delay_pos1 = (delay_pos1 + 1) % delay_line1.size();
        delay_pos2 = (delay_pos2 + 1) % delay_line2.size();
        delay_pos3 = (delay_pos3 + 1) % delay_line3.size();
    }
}

private:
    struct Vector3 {
        float x, y, z;
    };
    
    struct AudioSource3D {
        Vector3 position;
        Vector3 last_position;
        float volume;
        float doppler_factor;
        bool active;
    };
    
    Vector3 listener_position_{0, 0, 0};
    Vector3 listener_forward_{0, 0, -1};
    Vector3 listener_up_{0, 1, 0};
    Vector3 listener_right_{1, 0, 0};
    std::unordered_map<int, AudioSource3D> audio_sources_;
