// Nintendo Switch audio driver for the ReXGlue audio system: libnx audout (48 kHz, stereo, s16).
// From gh-recomp (Guardian Heroes, ours). Copied into the SDK tree (src/audio/nop/) by switch/sdk-edits-hcu.py and included by nop_audio_system.cpp
// on __SWITCH__ builds. The guest's 5.1 float frames (256 samples, big endian) are folded to stereo with the
// SDK's own mix, converted to s16 and played in 2 audout buffers of 1024 samples (21 ms each). The device's own
// clock paces everything: a frame counts as consumed when it goes into an audout buffer, and only then the
// client's semaphore is released, so the guest's audio clock runs at the real device rate. An empty queue plays
// silence (underrun counter). Latency = audio_maxqframes (6 -> 32 ms) + one audout buffer in flight (21 ms).
// One output thread serves all drivers (the guest registers and re-registers clients; audout opens once).
#pragma once

#include <switch.h>

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/audio/audio_driver.h>
#include <rex/audio/conversion.h>
#include <rex/audio/downmix.h>
#include <rex/thread.h>

extern "C" volatile int rex_nx_sound;  // 1 = play, 0 = silence (switch/host/switch_nx.cpp)

namespace rex::audio::nop {

class NxAudoutDriver;

constexpr uint32_t kNxChannelSamples = 256;                 // per guest frame and channel
constexpr uint32_t kNxFrameFloats = 6 * kNxChannelSamples;  // 5.1, sequential, big endian
constexpr uint32_t kNxBufferSamples = 1024;                 // per audout buffer (4096 bytes)
constexpr uint32_t kNxFramesPerBuffer = kNxBufferSamples / kNxChannelSamples;
constexpr uint32_t kNxBuffers = 2;
constexpr uint32_t kNxRing = 16;

class NxAudoutDriver : public AudioDriver {
 public:
  NxAudoutDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore);
  ~NxAudoutDriver() override;

  void SubmitFrame(uint32_t frame_ptr) override {
    const float* in = memory_->TranslateVirtual<const float*>(frame_ptr);
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == kNxRing) {  // never happens with the semaphore in place; keep the newest if it does
      head_ = (head_ + 1) % kNxRing;
      --count_;
      ++dropped_;
    }
    std::memcpy(ring_[(head_ + count_) % kNxRing], in, sizeof(float) * kNxFrameFloats);
    ++count_;
    ++submitted_;
  }

  // Output thread: the oldest queued frame, false when empty. Releases the client's semaphore for it.
  bool Pop(float* out) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (count_ == 0) return false;
      std::memcpy(out, ring_[head_], sizeof(float) * kNxFrameFloats);
      head_ = (head_ + 1) % kNxRing;
      --count_;
    }
    semaphore_->Release(1, nullptr);  // the frame is "played": the audio worker asks the guest for the next
    return true;
  }
  void Stats(uint64_t& submitted, uint64_t& dropped, uint32_t& depth) {
    std::lock_guard<std::mutex> lock(mutex_);
    submitted = submitted_;
    dropped = dropped_;
    depth = count_;
  }

 private:
  rex::thread::Semaphore* semaphore_;
  std::mutex mutex_;
  float ring_[kNxRing][kNxFrameFloats];
  uint32_t head_ = 0, count_ = 0;
  uint64_t submitted_ = 0, dropped_ = 0;
};

class NxAudout {
 public:
  static NxAudout& Get() {
    static NxAudout* instance = new NxAudout();  // lives until the process ends
    return *instance;
  }
  void Add(NxAudoutDriver* d) {
    std::lock_guard<std::mutex> lock(mutex_);
    drivers_.push_back(d);
    if (!started_) {
      started_ = true;
      std::thread([this] { Run(); }).detach();
    }
  }
  void Remove(NxAudoutDriver* d) {
    std::lock_guard<std::mutex> lock(mutex_);  // not while the output thread is filling
    drivers_.erase(std::remove(drivers_.begin(), drivers_.end(), d), drivers_.end());
  }

 private:
  NxAudout() { muted_ = access("sdmc:/switch/hcu/mute", F_OK) == 0; }

  // One audout buffer: kNxFramesPerBuffer guest frames, mixed over all drivers (silence when none has a frame).
  void Fill(AudioOutBuffer& buf) {
    int16_t* pcm = static_cast<int16_t*>(buf.buffer);
    const StereoFold fold = GetStereoFold();
    const float gain = (muted_ || !rex_nx_sound) ? 0.0f : GetOutputGain();
    static float frame[kNxFrameFloats];
    float stereo[kNxChannelSamples * 2];
    float mix[kNxChannelSamples * 2];
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t f = 0; f < kNxFramesPerBuffer; ++f) {
      std::memset(mix, 0, sizeof(mix));
      bool any = false;
      for (NxAudoutDriver* d : drivers_) {
        if (!d->Pop(frame)) continue;
        any = true;
        conversion::sequential_6_BE_to_interleaved_2_LE(stereo, frame, kNxChannelSamples, fold, gain);
        for (uint32_t i = 0; i < kNxChannelSamples * 2; ++i) mix[i] += stereo[i];
      }
      int16_t* out = pcm + f * kNxChannelSamples * 2;
      if (!any) {
        std::memset(out, 0, sizeof(int16_t) * kNxChannelSamples * 2);
        if (started_playing_) ++underruns_;
        continue;
      }
      float peak = 0.f;
      for (uint32_t i = 0; i < kNxChannelSamples * 2; ++i) {
        const float v = std::clamp(mix[i], -1.0f, 1.0f);
        peak = std::max(peak, std::fabs(v));
        out[i] = static_cast<int16_t>(v * 32767.0f);
      }
      peak_ = std::max(peak_, peak);
      ++consumed_;
    }
    buf.data_size = kNxBufferSamples * 2 * sizeof(int16_t);
    buf.data_offset = 0;
  }

  void Log() {
    uint64_t submitted = 0, dropped = 0, s, d;
    uint32_t depth = 0, dp;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (NxAudoutDriver* drv : drivers_) {
        drv->Stats(s, d, dp);
        submitted += s;
        dropped += d;
        depth += dp;
      }
    }
    std::printf("[nx] audio: submitted %llu consumed %llu frames, underruns %llu, dropped %llu, queue %u, peak %.3f%s\n",
                (unsigned long long)submitted, (unsigned long long)consumed_, (unsigned long long)underruns_,
                (unsigned long long)dropped, depth, peak_, muted_ ? " (muted)" : "");
    std::fflush(stdout);
    peak_ = 0.f;
  }

  void Run() {
    pthread_setname_np(pthread_self(), "Audio Driver");
    // Above the guest's time-sliced threads, below the GPU command processor (0x28); core 2 like the audio worker.
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x29);
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 0x4);
    if (R_FAILED(audoutInitialize()) || R_FAILED(audoutStartAudioOut())) {
      std::printf("[nx] audio: audout failed to start, the frames are consumed without a device\n");
      std::fflush(stdout);
      for (;;) {  // like the silent driver: the guest's audio clock still runs
        svcSleepThread(5'333'000);
        static float tmp[kNxFrameFloats];
        std::lock_guard<std::mutex> lock(mutex_);
        for (NxAudoutDriver* d : drivers_) d->Pop(tmp);
      }
    }
    std::printf("[nx] audio: audout %u Hz, %u ch, pcm format %d; %u buffers x %u samples, %u frames/buffer%s\n",
                audoutGetSampleRate(), audoutGetChannelCount(), static_cast<int>(audoutGetPcmFormat()), kNxBuffers,
                kNxBufferSamples, kNxFramesPerBuffer, muted_ ? " (muted by sdmc:/switch/hcu/mute)" : "");
    std::fflush(stdout);
    static AudioOutBuffer bufs[kNxBuffers];
    for (auto& b : bufs) {
      b.buffer = aligned_alloc(0x1000, 0x1000);
      b.buffer_size = 0x1000;
      std::memset(b.buffer, 0, 0x1000);
      Fill(b);
      audoutAppendAudioOutBuffer(&b);
    }
    started_playing_ = true;
    uint64_t last_log = armGetSystemTick();
    for (;;) {
      AudioOutBuffer* released = nullptr;
      u32 count = 0;
      Result rc = audoutWaitPlayFinish(&released, &count, 100'000'000ULL);
      if (R_SUCCEEDED(rc) && released && count) {  // the first released buffer; more come back on the next wait
        Fill(*released);
        audoutAppendAudioOutBuffer(released);
      }
      const uint64_t now = armGetSystemTick();
      if (now - last_log > 5 * armGetSystemTickFreq()) {
        last_log = now;
        Log();
      }
    }
  }

  std::mutex mutex_;
  std::vector<NxAudoutDriver*> drivers_;
  bool started_ = false, started_playing_ = false, muted_ = false;
  uint64_t consumed_ = 0, underruns_ = 0;
  float peak_ = 0.f;
};

inline NxAudoutDriver::NxAudoutDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {
  NxAudout::Get().Add(this);
}
inline NxAudoutDriver::~NxAudoutDriver() { NxAudout::Get().Remove(this); }

}  // namespace rex::audio::nop
