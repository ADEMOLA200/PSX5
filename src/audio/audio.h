#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

struct AudioFormat {
    int sample_rate;
    int channels;
    int bits_per_sample;
    bool is_float;
};

struct AudioBuffer {
    std::vector<float> data;
    size_t frames;
    int channels;
    double timestamp;
    bool is_ready;
};

class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual bool initialize(const AudioFormat& format) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void write_samples(const float* samples, size_t frame_count) = 0;
    virtual size_t get_buffer_size() const = 0;
    virtual double get_latency() const = 0;
};

class Audio {
private:
    std::unique_ptr<AudioDevice> device;
    AudioFormat current_format;
    std::vector<AudioBuffer> buffer_queue;
    std::mutex buffer_mutex;
    std::condition_variable buffer_cv;
    std::thread audio_thread;
    std::atomic<bool> running;
    
    struct APUState {
        float master_volume;
        float channel_volumes[8];
        bool channel_muted[8];
        int reverb_type;
        float reverb_depth;
        bool surround_enabled;
        int eq_bands[10];
    } apu_state;
    
    struct EffectsChain {
        bool compressor_enabled;
        float compressor_threshold;
        float compressor_ratio;
        bool limiter_enabled;
        float limiter_threshold;
        std::vector<float> delay_buffer;
        size_t delay_write_pos;
    } effects;
    
    void audio_thread_func();
    void process_audio_effects(float* samples, size_t frame_count);
    void apply_3d_audio(float* samples, size_t frame_count);
    void mix_channels(float* output, const float* input, size_t frame_count, int src_channels, int dst_channels);

public:
    Audio();
    ~Audio();
    
    bool initialize(const AudioFormat& format);
    void shutdown();
    void push_samples(const float* samples, int frame_count);
    void set_master_volume(float volume);
    void set_channel_volume(int channel, float volume);
    void mute_channel(int channel, bool muted);
    void enable_surround(bool enabled);
    void set_reverb(int type, float depth);
    void enable_3d_audio(bool enabled);
    
    void set_tempest_3d_params(float listener_x, float listener_y, float listener_z,
                              float forward_x, float forward_y, float forward_z,
                              float up_x, float up_y, float up_z);
    void add_audio_source(int source_id, float x, float y, float z, float volume);
    void update_audio_source(int source_id, float x, float y, float z);
    void remove_audio_source(int source_id);
    
    AudioFormat get_current_format() const { return current_format; }
    bool is_running() const { return running.load(); }
    size_t get_queued_frames() const;
};
