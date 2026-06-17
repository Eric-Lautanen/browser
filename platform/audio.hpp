#pragma once
#include "../tests/utility.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace browser::platform {

enum class AudioState { STOPPED, PLAYING, PAUSED };

struct WAVHeader {
    u16 audio_format;
    u16 num_channels;
    u32 sample_rate;
    u32 byte_rate;
    u16 block_align;
    u16 bits_per_sample;
    u32 data_size;
};

class AudioBuffer {
public:
    AudioBuffer() = default;
    Result<void> load_wav(const u8* data, u32 size);
    const f32* samples() const { return samples_.data(); }
    u32 num_samples() const { return num_samples_; }
    u32 sample_rate() const { return sample_rate_; }
    u16 num_channels() const { return num_channels_; }
    f32 duration() const;
    bool empty() const { return samples_.empty(); }

private:
    std::vector<f32> samples_;
    u32 num_samples_ = 0;
    u32 sample_rate_ = 44100;
    u16 num_channels_ = 1;
};

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    Result<void> play(const std::shared_ptr<AudioBuffer>& buffer, bool loop = false);
    void pause();
    void resume();
    void stop();
    AudioState state() const { return state_; }
    f32 current_time() const;
    void set_current_time(f32 t);
    f32 volume() const { return volume_; }
    void set_volume(f32 v) { volume_ = v < 0 ? 0 : (v > 1 ? 1 : v); }
    bool looping() const { return loop_; }
    void set_looping(bool l) { loop_ = l; }
    f32 duration() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioState state_ = AudioState::STOPPED;
    f32 volume_ = 1.0f;
    bool loop_ = false;
    std::shared_ptr<AudioBuffer> buffer_;
    f32 play_start_time_ = 0;
    u64 play_start_pos_ = 0;
    bool paused_ = false;
    f32 paused_position_ = 0;
};

} // namespace browser::platform
