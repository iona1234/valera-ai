#ifndef VALERA_RU_EAR_H
#define VALERA_RU_EAR_H

#include "audio_codec.h"
#include "wake_word.h"
#include "wake_word_audio_cache.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Free on-device Russian wake: AFE mono → MFCC + DTW vs Russian «Валера» templates.
// Not English MultiNet. Not Yandex. Not a renamed «Привет ESP» WakeNet.
class ValeraRuEar : public WakeWord {
public:
    ValeraRuEar();
    ~ValeraRuEar() override;

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list) override;
    void Feed(const std::vector<int16_t>& data) override;
    void FeedMono(const int16_t* data, size_t samples);
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) override;
    void Start() override;
    void Stop() override;
    size_t GetFeedSize() override;
    void EncodeWakeWordData() override;
    bool GetWakeWordOpus(std::vector<uint8_t>& opus) override;
    const std::string& GetLastDetectedWakeWord() const override { return last_detected_wake_word_; }

private:
    enum class GateState { kSilence, kSpeech, kCapture, kCooldown };

    struct Utterance {
        std::vector<int16_t> pcm;
    };

    static void WorkerThunk(void* arg);
    void WorkerLoop();
    void FeedSamples(const int16_t* data, size_t samples);
    bool MatchValera(const std::vector<int16_t>& pcm, float* best_score);
    static int FrameRms(const int16_t* data, size_t samples);

    AudioCodec* codec_ = nullptr;
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    std::string last_detected_wake_word_ = "Валера";
    std::atomic<bool> running_{false};
    std::atomic<bool> worker_stop_{false};

    GateState gate_ = GateState::kSilence;
    int speech_frames_ = 0;
    int silence_frames_ = 0;
    int cooldown_frames_ = 0;
    std::vector<int16_t> capture_;

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t worker_ = nullptr;

    TaskHandle_t wake_word_encode_task_ = nullptr;
    StaticTask_t* wake_word_encode_task_buffer_ = nullptr;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    WakeWordAudioCache wake_word_audio_cache_;
    std::deque<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;
};

#endif
