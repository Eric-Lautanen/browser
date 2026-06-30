#include "audio.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <windows.h>
#include <mmsystem.h>

namespace browser::platform {

Result<void> AudioBuffer::load_wav(const u8* data, u32 size) {
    if (size < 44) return Result<void>("WAV data too small");

    if (std::memcmp(data, "RIFF", 4) != 0) return Result<void>("Not a RIFF file");
    if (std::memcmp(data + 8, "WAVE", 4) != 0) return Result<void>("Not a WAVE file");

    WAVHeader hdr{};
    bool fmt_found = false;

    u32 offset = 12;
    while (offset + 8 <= size) {
        u32 chunk_size = static_cast<u32>(data[offset + 4]) |
                        (static_cast<u32>(data[offset + 5]) << 8) |
                        (static_cast<u32>(data[offset + 6]) << 16) |
                        (static_cast<u32>(data[offset + 7]) << 24);

        if (std::memcmp(data + offset, "fmt ", 4) == 0) {
            if (chunk_size >= 16 && offset + 8 + chunk_size <= size) {
                hdr.audio_format = static_cast<u16>(data[offset + 8]) |
                                  (static_cast<u16>(data[offset + 9]) << 8);
                hdr.num_channels = static_cast<u16>(data[offset + 10]) |
                                  (static_cast<u16>(data[offset + 11]) << 8);
                hdr.sample_rate = static_cast<u32>(data[offset + 12]) |
                                 (static_cast<u32>(data[offset + 13]) << 8) |
                                 (static_cast<u32>(data[offset + 14]) << 16) |
                                 (static_cast<u32>(data[offset + 15]) << 24);
                hdr.byte_rate = static_cast<u32>(data[offset + 16]) |
                               (static_cast<u32>(data[offset + 17]) << 8) |
                               (static_cast<u32>(data[offset + 18]) << 16) |
                               (static_cast<u32>(data[offset + 19]) << 24);
                hdr.block_align = static_cast<u16>(data[offset + 20]) |
                                 (static_cast<u16>(data[offset + 21]) << 8);
                hdr.bits_per_sample = static_cast<u16>(data[offset + 22]) |
                                     (static_cast<u16>(data[offset + 23]) << 8);
                fmt_found = true;
            }
        } else if (std::memcmp(data + offset, "data", 4) == 0) {
            if (!fmt_found || hdr.audio_format != 1)
                return Result<void>("Unsupported WAV format (PCM required)");
            hdr.data_size = chunk_size;
            if (offset + 8 + chunk_size > size) hdr.data_size = size - offset - 8;

            const u8* pcm_data = data + offset + 8;
            u32 pcm_samples = hdr.data_size / (hdr.bits_per_sample / 8);

            samples_.resize(pcm_samples);
            num_samples_ = pcm_samples;
            sample_rate_ = hdr.sample_rate;
            num_channels_ = hdr.num_channels;

            if (hdr.bits_per_sample == 16) {
                for (u32 i = 0; i < pcm_samples; i++) {
                    i16 val = static_cast<i16>(pcm_data[i * 2]) |
                              (static_cast<i16>(pcm_data[i * 2 + 1]) << 8);
                    samples_[i] = val / 32768.0f;
                }
            } else if (hdr.bits_per_sample == 8) {
                for (u32 i = 0; i < pcm_samples; i++)
                    samples_[i] = (pcm_data[i] / 128.0f) - 1.0f;
            } else if (hdr.bits_per_sample == 24) {
                for (u32 i = 0; i < pcm_samples; i++) {
                    i32 val = static_cast<i32>(pcm_data[i * 3]) |
                             (static_cast<i32>(pcm_data[i * 3 + 1]) << 8) |
                             (static_cast<i32>(pcm_data[i * 3 + 2]) << 16);
                    if (val & 0x800000) val |= ~0xFFFFFF;
                    samples_[i] = val / 8388608.0f;
                }
            }
            return {};
        }
        offset += 8 + chunk_size;
        if (chunk_size % 2) offset++;
    }
    return Result<void>("No data chunk found in WAV");
}

f32 AudioBuffer::duration() const {
    if (sample_rate_ == 0) return 0;
    return static_cast<f32>(num_samples_) / sample_rate_ / num_channels_;
}

struct AudioPlayer::Impl {
    HWAVEOUT hwo = nullptr;
    WAVEHDR wh{};
    std::vector<u8> playback_buffer;
    bool device_open = false;

    ~Impl() {
        if (hwo) {
            waveOutReset(hwo);
            waveOutUnprepareHeader(hwo, &wh, sizeof(WAVEHDR));
            waveOutClose(hwo);
        }
    }
};

AudioPlayer::AudioPlayer() : impl_(std::make_unique<Impl>()) {}
AudioPlayer::~AudioPlayer() = default;

Result<void> AudioPlayer::play(const std::shared_ptr<AudioBuffer>& buffer, bool loop) {
    stop();
    if (!buffer || buffer->empty()) return Result<void>("Empty audio buffer");

    buffer_ = buffer;
    loop_ = loop;
    state_ = AudioState::PLAYING;
    paused_ = false;
    paused_position_ = 0;

    u32 num_samples = buffer->num_samples();
    u16 channels = buffer->num_channels();
    u32 sample_rate = buffer->sample_rate();

    // Convert float samples to 16-bit PCM
    u32 data_size = num_samples * 2;
    impl_->playback_buffer.resize(data_size);
    for (u32 i = 0; i < num_samples; i++) {
        i16 val = static_cast<i16>(std::max(-1.0f, std::min(1.0f, buffer->samples()[i])) * 32767);
        impl_->playback_buffer[i * 2] = static_cast<u8>(val & 0xFF);
        impl_->playback_buffer[i * 2 + 1] = static_cast<u8>((val >> 8) & 0xFF);
    }

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = channels;
    wfx.nSamplesPerSec = sample_rate;
    wfx.nAvgBytesPerSec = sample_rate * channels * 2;
    wfx.nBlockAlign = channels * 2;
    wfx.wBitsPerSample = 16;
    wfx.cbSize = 0;

    MMRESULT res = waveOutOpen(&impl_->hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR)
        return Result<void>("waveOutOpen failed: " + std::to_string(res));

    impl_->device_open = true;
    impl_->wh.lpData = reinterpret_cast<LPSTR>(impl_->playback_buffer.data());
    impl_->wh.dwBufferLength = data_size;
    impl_->wh.dwFlags = 0;
    impl_->wh.dwLoops = 0;

    res = waveOutPrepareHeader(impl_->hwo, &impl_->wh, sizeof(WAVEHDR));
    if (res != MMSYSERR_NOERROR) {
        waveOutClose(impl_->hwo);
        impl_->hwo = nullptr;
        return Result<void>("waveOutPrepareHeader failed");
    }

    res = waveOutWrite(impl_->hwo, &impl_->wh, sizeof(WAVEHDR));
    if (res != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(impl_->hwo, &impl_->wh, sizeof(WAVEHDR));
        waveOutClose(impl_->hwo);
        impl_->hwo = nullptr;
        return Result<void>("waveOutWrite failed");
    }

    play_start_time_ = 0;
    play_start_pos_ = 0;
    return {};
}

void AudioPlayer::pause() {
    if (state_ != AudioState::PLAYING) return;
    if (impl_->hwo) waveOutPause(impl_->hwo);
    state_ = AudioState::PAUSED;
    paused_ = true;
    paused_position_ = current_time();
}

void AudioPlayer::resume() {
    if (state_ != AudioState::PAUSED) return;
    if (impl_->hwo) waveOutRestart(impl_->hwo);
    state_ = AudioState::PLAYING;
    paused_ = false;
}

void AudioPlayer::stop() {
    if (impl_->hwo) {
        waveOutReset(impl_->hwo);
        waveOutUnprepareHeader(impl_->hwo, &impl_->wh, sizeof(WAVEHDR));
        waveOutClose(impl_->hwo);
        impl_->hwo = nullptr;
        impl_->device_open = false;
    }
    state_ = AudioState::STOPPED;
    buffer_.reset();
}

f32 AudioPlayer::current_time() const {
    if (!buffer_) return 0;
    if (paused_) return paused_position_;
    if (!impl_->device_open || !impl_->hwo) return 0;

    MMTIME mmt{};
    mmt.wType = TIME_SAMPLES;
    waveOutGetPosition(impl_->hwo, &mmt, sizeof(mmt));
    if (mmt.wType == TIME_SAMPLES)
        return static_cast<f32>(mmt.u.sample) / buffer_->sample_rate();
    return 0;
}

void AudioPlayer::set_current_time(f32 t) {
    if (!buffer_ || !impl_->hwo) return;
    u32 sample_pos = static_cast<u32>(t * buffer_->sample_rate() * buffer_->num_channels());
    if (sample_pos >= buffer_->num_samples()) sample_pos = 0;
    // For simplicity, just stop on seek (full re-implementation needed for proper seeking)
    stop();
}

f32 AudioPlayer::duration() const {
    return buffer_ ? buffer_->duration() : 0;
}

} // namespace browser::platform
