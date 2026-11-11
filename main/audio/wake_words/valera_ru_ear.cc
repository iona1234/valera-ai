#include "valera_ru_ear.h"

#include "audio_service.h"
#include "board.h"
#include "settings.h"
#include "system_info.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#define TAG "ValeraRuEar"

namespace {

constexpr int kSampleRate = 16000;
constexpr int kFrameMs = 20;
constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000;  // 320
constexpr int kSpeechFramesNeed = 8;                         // ~160 ms
constexpr int kCaptureFrames = 60;                           // ~1.2 s after trigger
constexpr int kSilenceEndFrames = 15;                        // ~300 ms trailing silence
constexpr int kCooldownFrames = 80;                          // ~1.6 s
constexpr int kEnergyThreshold = 450;
constexpr size_t kMaxQueue = 2;

}  // namespace

ValeraRuEar::ValeraRuEar() = default;

ValeraRuEar::~ValeraRuEar() {
    worker_stop_ = true;
    if (queue_ != nullptr) {
        Utterance* poison = nullptr;
        xQueueSend(queue_, &poison, pdMS_TO_TICKS(100));
    }
    // Worker task deletes itself after receiving the poison pill.
    for (int i = 0; i < 50 && worker_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (queue_ != nullptr) {
        Utterance* leftover = nullptr;
        while (xQueueReceive(queue_, &leftover, 0) == pdTRUE) {
            delete leftover;
        }
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }
    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }
}

bool ValeraRuEar::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    (void)models_list;
    codec_ = codec;
    capture_.reserve(kFrameSamples * (kSpeechFramesNeed + kCaptureFrames));
    queue_ = xQueueCreate(kMaxQueue, sizeof(Utterance*));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create STT queue");
        return false;
    }
#if CONFIG_SEND_WAKE_WORD_DATA
    if (!wake_word_audio_cache_.Initialize(16000 * 2)) {
        ESP_LOGW(TAG, "Wake-word audio upload disabled: PSRAM cache allocation failed");
    }
#endif
    worker_stop_ = false;
    if (xTaskCreate(WorkerThunk, "valera_ear", 8192, this, 2, &worker_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start Valera ear worker");
        return false;
    }
    ESP_LOGW(TAG,
             "Russian cloud ear ready (Yandex SpeechKit ru-RU). "
             "Needs NVS valera/api_key + valera/folder_id. BOOT remains fallback.");
    return true;
}

void ValeraRuEar::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = std::move(callback);
}

void ValeraRuEar::Start() {
    running_ = true;
    gate_ = GateState::kSilence;
    speech_frames_ = 0;
    silence_frames_ = 0;
    cooldown_frames_ = 0;
    capture_.clear();
}

void ValeraRuEar::Stop() {
    running_ = false;
    capture_.clear();
    gate_ = GateState::kSilence;
}

void ValeraRuEar::Feed(const std::vector<int16_t>& data) {
    if (data.empty()) {
        return;
    }
    FeedSamples(data.data(), data.size());
}

void ValeraRuEar::FeedMono(const int16_t* data, size_t samples) {
    FeedSamples(data, samples);
}

size_t ValeraRuEar::GetFeedSize() {
    return kFrameSamples;
}

int ValeraRuEar::FrameRms(const int16_t* data, size_t samples) {
    if (data == nullptr || samples == 0) {
        return 0;
    }
    int64_t acc = 0;
    for (size_t i = 0; i < samples; ++i) {
        int32_t v = data[i];
        acc += v * v;
    }
    return (int)std::sqrt((double)acc / (double)samples);
}

void ValeraRuEar::FeedSamples(const int16_t* data, size_t samples) {
    if (!running_ || data == nullptr || samples == 0) {
        return;
    }

#if CONFIG_SEND_WAKE_WORD_DATA
    wake_word_audio_cache_.Store(data, samples);
#endif

    size_t offset = 0;
    while (offset + kFrameSamples <= samples) {
        const int16_t* frame = data + offset;
        const int rms = FrameRms(frame, kFrameSamples);
        const bool loud = rms >= kEnergyThreshold;

        switch (gate_) {
            case GateState::kSilence:
                if (loud) {
                    speech_frames_ = 1;
                    capture_.assign(frame, frame + kFrameSamples);
                    gate_ = GateState::kSpeech;
                }
                break;
            case GateState::kSpeech:
                capture_.insert(capture_.end(), frame, frame + kFrameSamples);
                if (loud) {
                    ++speech_frames_;
                    if (speech_frames_ >= kSpeechFramesNeed) {
                        silence_frames_ = 0;
                        gate_ = GateState::kCapture;
                    }
                } else {
                    speech_frames_ = 0;
                    capture_.clear();
                    gate_ = GateState::kSilence;
                }
                break;
            case GateState::kCapture:
                capture_.insert(capture_.end(), frame, frame + kFrameSamples);
                if (loud) {
                    silence_frames_ = 0;
                } else {
                    ++silence_frames_;
                }
                if (silence_frames_ >= kSilenceEndFrames ||
                    capture_.size() >= (size_t)kFrameSamples * (kSpeechFramesNeed + kCaptureFrames)) {
                    auto* utt = new (std::nothrow) Utterance{std::move(capture_)};
                    capture_.clear();
                    capture_.reserve(kFrameSamples * (kSpeechFramesNeed + kCaptureFrames));
                    if (utt != nullptr) {
                        if (xQueueSend(queue_, &utt, 0) != pdTRUE) {
                            delete utt;
                        }
                    }
                    cooldown_frames_ = kCooldownFrames;
                    gate_ = GateState::kCooldown;
                }
                break;
            case GateState::kCooldown:
                if (--cooldown_frames_ <= 0) {
                    gate_ = GateState::kSilence;
                }
                break;
        }
        offset += kFrameSamples;
    }
}

void ValeraRuEar::WorkerThunk(void* arg) {
    static_cast<ValeraRuEar*>(arg)->WorkerLoop();
}

void ValeraRuEar::WorkerLoop() {
    while (!worker_stop_) {
        Utterance* utt = nullptr;
        if (xQueueReceive(queue_, &utt, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }
        if (utt == nullptr) {
            break;
        }
        std::string text;
        bool ok = Recognize(utt->pcm, &text);
        delete utt;
        if (!ok || text.empty()) {
            continue;
        }
        ESP_LOGI(TAG, "Yandex STT: \"%s\"", text.c_str());
        if (!TextHasValera(text)) {
            continue;
        }
        last_detected_wake_word_ = "Валера";
        running_ = false;
        if (wake_word_detected_callback_) {
            wake_word_detected_callback_(last_detected_wake_word_);
        }
    }
    worker_ = nullptr;
    vTaskDelete(nullptr);
}

bool ValeraRuEar::CredentialsReady(std::string* api_key, std::string* folder_id) const {
    Settings settings("valera", false);
    *api_key = settings.GetString("api_key");
    *folder_id = settings.GetString("folder_id");
    return !api_key->empty() && !folder_id->empty();
}

bool ValeraRuEar::Recognize(const std::vector<int16_t>& pcm, std::string* text) {
    text->clear();
    std::string api_key;
    std::string folder_id;
    if (!CredentialsReady(&api_key, &folder_id)) {
        if (!missing_creds_logged_) {
            ESP_LOGW(TAG,
                     "Yandex credentials missing (NVS valera/api_key, valera/folder_id). "
                     "Voice «Валера» idle; use BOOT or MCP self.valera.set_yandex_stt.");
            missing_creds_logged_ = true;
        }
        return false;
    }
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }
    auto http = network->CreateHttp(0);
    if (!http) {
        return false;
    }

    char url[256];
    std::snprintf(url, sizeof(url),
                  "https://stt.api.cloud.yandex.net/speech/v1/stt:recognize"
                  "?lang=ru-RU&folderId=%s&format=lpcm&sampleRateHertz=16000",
                  folder_id.c_str());

    http->SetHeader("Authorization", std::string("Api-Key ") + api_key);
    http->SetHeader("Content-Type", "application/octet-stream");

    std::string body(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(int16_t));
    http->SetContent(std::move(body));
    if (auto opened = http->Open("POST", url); !opened) {
        ESP_LOGW(TAG, "Yandex STT open failed");
        return false;
    }
    auto status = http->GetStatusCode();
    std::string resp = http->ReadAll();
    http->Close();
    if (!status || *status != 200) {
        ESP_LOGW(TAG, "Yandex STT HTTP %d", status ? *status : -1);
        return false;
    }

    // API may return plain text or {"result":"..."}.
    cJSON* root = cJSON_Parse(resp.c_str());
    if (root != nullptr) {
        cJSON* result = cJSON_GetObjectItem(root, "result");
        if (cJSON_IsString(result) && result->valuestring != nullptr) {
            *text = result->valuestring;
        }
        cJSON_Delete(root);
    }
    if (text->empty()) {
        // Trim quotes/whitespace from raw body.
        size_t start = 0;
        size_t end = resp.size();
        while (start < end && std::isspace(static_cast<unsigned char>(resp[start]))) ++start;
        while (end > start && std::isspace(static_cast<unsigned char>(resp[end - 1]))) --end;
        if (end > start + 1 && resp[start] == '"' && resp[end - 1] == '"') {
            ++start;
            --end;
        }
        *text = resp.substr(start, end - start);
    }
    return !text->empty();
}

bool ValeraRuEar::TextHasValera(const std::string& text) {
    // Match Russian stem «валер» in UTF-8 (covers Валера/Валеру/валера).
    // Source file is UTF-8; plain literals (not u8"...") stay const char*.
    static const char* kNeedles[] = {
        "валера", "Валера", "ВАЛЕРА", "Валеру", "валеру",
        "Валер",  "валер",  "ВАЛЕР",
    };
    for (const char* needle : kNeedles) {
        if (text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void ValeraRuEar::EncodeWakeWordData() {
    const size_t stack_size = 4096 * 7;
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ =
            (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
        assert(wake_word_encode_task_stack_ != nullptr);
    }
    if (wake_word_encode_task_buffer_ == nullptr) {
        wake_word_encode_task_buffer_ =
            (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
        assert(wake_word_encode_task_buffer_ != nullptr);
    }

    wake_word_encode_task_ = xTaskCreateStatic(
        [](void* arg) {
            auto* this_ = (ValeraRuEar*)arg;
            auto start_time = esp_timer_get_time();
            esp_opus_enc_config_t opus_enc_cfg = AS_OPUS_ENC_CONFIG();
            void* encoder_handle = nullptr;
            auto ret = esp_opus_enc_open(&opus_enc_cfg, sizeof(esp_opus_enc_config_t), &encoder_handle);
            if (encoder_handle == nullptr) {
                ESP_LOGE(TAG, "Failed to create audio encoder, error code: %d", ret);
                this_->wake_word_audio_cache_.Clear();
                std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                this_->wake_word_opus_.push_back(std::vector<uint8_t>());
                this_->wake_word_cv_.notify_all();
                vTaskDelete(nullptr);
                return;
            }
            int frame_size = 0;
            int outbuf_size = 0;
            esp_opus_enc_get_frame_size(encoder_handle, &frame_size, &outbuf_size);
            frame_size = frame_size / sizeof(int16_t);
            int packets = 0;
            std::vector<int16_t> in_buffer(frame_size);
            esp_audio_enc_in_frame_t in = {};
            esp_audio_enc_out_frame_t out = {};
            const size_t cached_samples = this_->wake_word_audio_cache_.Size();
            for (size_t offset = 0;
                 offset + static_cast<size_t>(frame_size) <= cached_samples;
                 offset += frame_size) {
                if (this_->wake_word_audio_cache_.Read(offset, in_buffer.data(), frame_size) !=
                    static_cast<size_t>(frame_size)) {
                    break;
                }
                std::vector<uint8_t> opus_buf(outbuf_size);
                in.buffer = reinterpret_cast<uint8_t*>(in_buffer.data());
                in.len = frame_size * sizeof(int16_t);
                out.buffer = opus_buf.data();
                out.len = outbuf_size;
                out.encoded_bytes = 0;
                ret = esp_opus_enc_process(encoder_handle, &in, &out);
                if (ret == ESP_AUDIO_ERR_OK) {
                    std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                    this_->wake_word_opus_.emplace_back(opus_buf.data(),
                                                       opus_buf.data() + out.encoded_bytes);
                    this_->wake_word_cv_.notify_all();
                    packets++;
                }
            }
            this_->wake_word_audio_cache_.Clear();
            esp_opus_enc_close(encoder_handle);
            ESP_LOGI(TAG, "Encode wake word opus %d packets in %ld ms", packets,
                     (long)((esp_timer_get_time() - start_time) / 1000));
            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
            vTaskDelete(NULL);
        },
        "encode_wake_word", stack_size, this, 2, wake_word_encode_task_stack_,
        wake_word_encode_task_buffer_);
}

bool ValeraRuEar::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() { return !wake_word_opus_.empty(); });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
