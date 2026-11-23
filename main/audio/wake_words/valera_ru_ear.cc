#include "valera_ru_ear.h"

#include "audio_service.h"
#include "valera_templates.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

#define TAG "ValeraRuEar"

namespace {

constexpr int kSampleRate = kValeraSr;
constexpr int kFrameMs = 20;
constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000;  // 320
constexpr int kSpeechFramesNeed = 6;                         // ~120 ms
constexpr int kCaptureFrames = 55;                           // ~1.1 s
constexpr int kSilenceEndFrames = 12;                        // ~240 ms
constexpr int kCooldownFrames = 70;
constexpr int kEnergyThreshold = 500;
constexpr size_t kMaxQueue = 2;
// Normalized DTW average cost; lower is closer. Tuned vs Russian TTS templates
// of «Валера» (held-out ~0.4, near-words ~0.9–1.4, other words ~1.8+).
constexpr float kMatchThreshold = 1.20f;

float HzToMel(float hz) { return 2595.0f * log10f(1.0f + hz / 700.0f); }
float MelToHz(float mel) { return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f); }

void BuildMelFilterbank(float* fb /* n_mels * (n_fft/2+1) */) {
    const int n_fft = kValeraNFft;
    const int n_bins = n_fft / 2 + 1;
    const int n_mels = kValeraNMels;
    const float fmin = 20.0f;
    const float fmax = kSampleRate / 2.0f;
    float mels[kValeraNMels + 2];
    int bins[kValeraNMels + 2];
    for (int i = 0; i < n_mels + 2; ++i) {
        float mel = HzToMel(fmin) + (HzToMel(fmax) - HzToMel(fmin)) * i / (n_mels + 1);
        mels[i] = mel;
        bins[i] = (int)floorf((n_fft + 1) * MelToHz(mel) / kSampleRate);
    }
    std::memset(fb, 0, sizeof(float) * n_mels * n_bins);
    for (int i = 0; i < n_mels; ++i) {
        int left = bins[i];
        int center = bins[i + 1];
        int right = bins[i + 2];
        for (int j = left; j < center; ++j) {
            if (j >= 0 && j < n_bins && center != left) {
                fb[i * n_bins + j] = (float)(j - left) / (float)(center - left);
            }
        }
        for (int j = center; j < right; ++j) {
            if (j >= 0 && j < n_bins && right != center) {
                fb[i * n_bins + j] = (float)(right - j) / (float)(right - center);
            }
        }
    }
}

bool ExtractMfcc(const std::vector<int16_t>& pcm_in, std::vector<float>* out_frames) {
    if (pcm_in.size() < (size_t)kValeraNFft) {
        return false;
    }
    std::vector<float> pcm(pcm_in.size());
    double mean = 0;
    for (size_t i = 0; i < pcm_in.size(); ++i) mean += pcm_in[i];
    mean /= (double)pcm_in.size();
    float peak = 1e-6f;
    for (size_t i = 0; i < pcm_in.size(); ++i) {
        pcm[i] = (float)(pcm_in[i] - mean);
        peak = std::max(peak, fabsf(pcm[i]));
    }
    for (float& v : pcm) v /= peak;

    // Energy trim (10 ms hops), same idea as template generator.
    const int trim_hop = kValeraHop;
    double emax = 0;
    for (size_t i = 0; i + trim_hop <= pcm.size(); i += trim_hop) {
        double e = 0;
        for (int j = 0; j < trim_hop; ++j) e += pcm[i + j] * pcm[i + j];
        emax = std::max(emax, sqrt(e / trim_hop));
    }
    const double thr = std::max(0.02, emax * 0.08);
    int first = -1, last = -1;
    for (size_t i = 0; i + trim_hop <= pcm.size(); i += trim_hop) {
        double e = 0;
        for (int j = 0; j < trim_hop; ++j) e += pcm[i + j] * pcm[i + j];
        if (sqrt(e / trim_hop) >= thr) {
            if (first < 0) first = (int)i;
            last = (int)(i + trim_hop);
        }
    }
    if (first < 0) return false;
    pcm.assign(pcm.begin() + first, pcm.begin() + std::min(last, (int)pcm.size()));

    static float mel_fb[kValeraNMels * (kValeraNFft / 2 + 1)];
    static float hann[kValeraNFft];
    static bool tables_ready = false;
    if (!tables_ready) {
        BuildMelFilterbank(mel_fb);
        for (int i = 0; i < kValeraNFft; ++i) {
            hann[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (kValeraNFft - 1)));
        }
        tables_ready = true;
    }

    const int n_bins = kValeraNFft / 2 + 1;
    std::vector<float> re(kValeraNFft);
    std::vector<float> im(kValeraNFft);
    std::vector<float> spectrum(n_bins);
    out_frames->clear();
    out_frames->reserve(kValeraMaxFrames * kValeraNMfcc);

    auto fft_radix2 = [](float* re_v, float* im_v, int n) {
        int j = 0;
        for (int i = 1; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(re_v[i], re_v[j]);
                std::swap(im_v[i], im_v[j]);
            }
        }
        for (int len = 2; len <= n; len <<= 1) {
            float ang = -2.0f * (float)M_PI / len;
            float wlen_re = cosf(ang);
            float wlen_im = sinf(ang);
            for (int i = 0; i < n; i += len) {
                float w_re = 1.0f;
                float w_im = 0.0f;
                for (int k = 0; k < len / 2; ++k) {
                    float u_re = re_v[i + k];
                    float u_im = im_v[i + k];
                    float v_re = re_v[i + k + len / 2] * w_re - im_v[i + k + len / 2] * w_im;
                    float v_im = re_v[i + k + len / 2] * w_im + im_v[i + k + len / 2] * w_re;
                    re_v[i + k] = u_re + v_re;
                    im_v[i + k] = u_im + v_im;
                    re_v[i + k + len / 2] = u_re - v_re;
                    im_v[i + k + len / 2] = u_im - v_im;
                    float next_w_re = w_re * wlen_re - w_im * wlen_im;
                    w_im = w_re * wlen_im + w_im * wlen_re;
                    w_re = next_w_re;
                }
            }
        }
    };

    for (size_t start = 0; start + kValeraNFft <= pcm.size(); start += kValeraHop) {
        for (int i = 0; i < kValeraNFft; ++i) {
            re[i] = pcm[start + i] * hann[i];
            im[i] = 0.0f;
        }
        fft_radix2(re.data(), im.data(), kValeraNFft);
        for (int k = 0; k < n_bins; ++k) {
            spectrum[k] = re[k] * re[k] + im[k] * im[k];
        }
        float logmel[kValeraNMels];
        for (int m = 0; m < kValeraNMels; ++m) {
            float e = 0;
            const float* row = mel_fb + m * n_bins;
            for (int b = 0; b < n_bins; ++b) e += row[b] * spectrum[b];
            logmel[m] = logf(e + 1e-10f);
        }
        for (int k = 0; k < kValeraNMfcc; ++k) {
            float sum = 0;
            for (int n = 0; n < kValeraNMels; ++n) {
                sum += logmel[n] * cosf((float)M_PI * k * (2 * n + 1) / (2.0f * kValeraNMels));
            }
            out_frames->push_back(sum);
        }
        if ((int)out_frames->size() / kValeraNMfcc >= kValeraMaxFrames) break;
    }

    const int frames = (int)out_frames->size() / kValeraNMfcc;
    if (frames < 8) return false;

    // CMVN
    for (int c = 0; c < kValeraNMfcc; ++c) {
        float mean = 0, var = 0;
        for (int f = 0; f < frames; ++f) mean += (*out_frames)[f * kValeraNMfcc + c];
        mean /= frames;
        for (int f = 0; f < frames; ++f) {
            float d = (*out_frames)[f * kValeraNMfcc + c] - mean;
            var += d * d;
        }
        float std = sqrtf(var / frames) + 1e-5f;
        for (int f = 0; f < frames; ++f) {
            float& v = (*out_frames)[f * kValeraNMfcc + c];
            v = (v - mean) / std;
        }
    }
    return true;
}

float FrameDist(const float* a, const float* b) {
    float s = 0;
    // Emphasize lower cepstra (1..8); skip C0 slightly by downweighting.
    for (int i = 0; i < kValeraNMfcc; ++i) {
        float w = (i == 0) ? 0.35f : 1.0f;
        float d = a[i] - b[i];
        s += w * d * d;
    }
    return sqrtf(s);
}

float DtwAverage(const float* a, int fa, const float* b, int fb) {
    // Classic DTW with Sakoe-Chiba band ~30% of max length.
    const int band = std::max(4, (int)(0.35f * std::max(fa, fb)));
    const float inf = 1e9f;
    std::vector<float> prev(fb + 1, inf), cur(fb + 1, inf);
    prev[0] = 0;
    for (int i = 1; i <= fa; ++i) {
        std::fill(cur.begin(), cur.end(), inf);
        int j0 = std::max(1, i - band);
        int j1 = std::min(fb, i + band);
        for (int j = j0; j <= j1; ++j) {
            float cost = FrameDist(a + (i - 1) * kValeraNMfcc, b + (j - 1) * kValeraNMfcc);
            float best = prev[j];
            if (cur[j - 1] < best) best = cur[j - 1];
            if (prev[j - 1] < best) best = prev[j - 1];
            cur[j] = cost + best;
        }
        prev.swap(cur);
    }
    if (prev[fb] >= inf / 2) return inf;
    return prev[fb] / (float)(fa + fb);
}

}  // namespace

ValeraRuEar::ValeraRuEar() = default;

ValeraRuEar::~ValeraRuEar() {
    worker_stop_ = true;
    if (queue_ != nullptr) {
        Utterance* poison = nullptr;
        xQueueSend(queue_, &poison, pdMS_TO_TICKS(100));
    }
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
        ESP_LOGE(TAG, "Failed to create match queue");
        return false;
    }
#if CONFIG_SEND_WAKE_WORD_DATA
    if (!wake_word_audio_cache_.Initialize(16000 * 2)) {
        ESP_LOGW(TAG, "Wake-word audio upload disabled: PSRAM cache allocation failed");
    }
#endif
    worker_stop_ = false;
    if (xTaskCreate(WorkerThunk, "valera_ear", 12288, this, 2, &worker_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start Valera local ear worker");
        return false;
    }
    ESP_LOGI(TAG,
             "Free local Russian ear ready (%d MFCC templates of «Валера»). "
             "No cloud STT. BOOT remains fallback.",
             kValeraTemplateCount);
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
    if (!data.empty()) FeedSamples(data.data(), data.size());
}

void ValeraRuEar::FeedMono(const int16_t* data, size_t samples) {
    FeedSamples(data, samples);
}

size_t ValeraRuEar::GetFeedSize() { return kFrameSamples; }

int ValeraRuEar::FrameRms(const int16_t* data, size_t samples) {
    if (data == nullptr || samples == 0) return 0;
    int64_t acc = 0;
    for (size_t i = 0; i < samples; ++i) {
        int32_t v = data[i];
        acc += v * v;
    }
    return (int)std::sqrt((double)acc / (double)samples);
}

void ValeraRuEar::FeedSamples(const int16_t* data, size_t samples) {
    if (!running_ || data == nullptr || samples == 0) return;

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
                    capture_.size() >=
                        (size_t)kFrameSamples * (kSpeechFramesNeed + kCaptureFrames)) {
                    auto* utt = new (std::nothrow) Utterance{std::move(capture_)};
                    capture_.clear();
                    capture_.reserve(kFrameSamples * (kSpeechFramesNeed + kCaptureFrames));
                    if (utt != nullptr && xQueueSend(queue_, &utt, 0) != pdTRUE) {
                        delete utt;
                    }
                    cooldown_frames_ = kCooldownFrames;
                    gate_ = GateState::kCooldown;
                }
                break;
            case GateState::kCooldown:
                if (--cooldown_frames_ <= 0) gate_ = GateState::kSilence;
                break;
        }
        offset += kFrameSamples;
    }
}

void ValeraRuEar::WorkerThunk(void* arg) {
    static_cast<ValeraRuEar*>(arg)->WorkerLoop();
}

bool ValeraRuEar::MatchValera(const std::vector<int16_t>& pcm, float* best_score) {
    std::vector<float> feats;
    if (!ExtractMfcc(pcm, &feats)) return false;
    const int frames = (int)feats.size() / kValeraNMfcc;
    float best = 1e9f;
    for (int t = 0; t < kValeraTemplateCount; ++t) {
        float score = DtwAverage(feats.data(), frames, kValeraTemplates[t].data,
                                 kValeraTemplates[t].frames);
        if (score < best) best = score;
    }
    *best_score = best;
    return best <= kMatchThreshold;
}

void ValeraRuEar::WorkerLoop() {
    while (!worker_stop_) {
        Utterance* utt = nullptr;
        if (xQueueReceive(queue_, &utt, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        if (utt == nullptr) break;
        float score = 0;
        bool hit = MatchValera(utt->pcm, &score);
        ESP_LOGI(TAG, "Local «Валера» score=%.2f threshold=%.2f hit=%d", score, kMatchThreshold,
                 (int)hit);
        delete utt;
        if (!hit) continue;
        last_detected_wake_word_ = "Валера";
        running_ = false;
        if (wake_word_detected_callback_) {
            wake_word_detected_callback_(last_detected_wake_word_);
        }
    }
    worker_ = nullptr;
    vTaskDelete(nullptr);
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
            auto ret =
                esp_opus_enc_open(&opus_enc_cfg, sizeof(esp_opus_enc_config_t), &encoder_handle);
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
            for (size_t offset = 0; offset + (size_t)frame_size <= cached_samples;
                 offset += frame_size) {
                if (this_->wake_word_audio_cache_.Read(offset, in_buffer.data(), frame_size) !=
                    (size_t)frame_size) {
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
