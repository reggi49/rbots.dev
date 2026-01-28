#include "wake_word_runtime.h"

#include <new>
#include <math.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "wake_word_model.h"

namespace {

constexpr size_t kTensorArenaSize = 50 * 1024; // 78->50kb to free heap
constexpr int kSampleRate = 16000;
constexpr int kFeatureFrames = 49;
constexpr int kFeatureBins = 10;
constexpr int kWinSamples = 480; // 30 ms @ 16kHz
constexpr int kHopSamples = 320; // 20 ms @ 16kHz
constexpr int kFftSamples = 512;
constexpr int kFftBins = (kFftSamples / 2) + 1;
constexpr float kLogFloor = 1e-6f;

alignas(16) static uint8_t tensor_arena[kTensorArenaSize];
alignas(tflite::MicroInterpreter) static uint8_t interpreter_buffer[sizeof(tflite::MicroInterpreter)];

static tflite::MicroMutableOpResolver<25> resolver;
static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input_tensor = nullptr;
static bool tensors_ready = false;

static bool feature_tables_ready = false;
static float hamming_window[kWinSamples];
static float mel_filters[kFeatureBins][kFftBins];

} // namespace

static const char *TAG_WW = "WW";

static const char *tensor_type_name(TfLiteType type)
{
    switch (type) {
        case kTfLiteFloat32: return "float32";
        case kTfLiteInt16: return "int16";
        case kTfLiteInt8: return "int8";
        case kTfLiteUInt8: return "uint8";
        case kTfLiteInt32: return "int32";
        default: return "unknown";
    }
}

static void log_tensor_info(const char *label, TfLiteTensor *tensor)
{
    if (!tensor || !tensor->dims) return;
    const int dims = tensor->dims->size;
    char shape[64] = {0};
    int pos = 0;
    pos += snprintf(shape + pos, sizeof(shape) - pos, "[");
    for (int i = 0; i < dims; i++) {
        if (pos >= (int)sizeof(shape)) break;
        pos += snprintf(shape + pos, sizeof(shape) - pos, "%d%s",
                        tensor->dims->data[i], (i + 1 < dims) ? "x" : "");
    }
    if (pos < (int)sizeof(shape)) {
        snprintf(shape + pos, sizeof(shape) - pos, "]");
    }
    const char *type_name = tensor_type_name(tensor->type);
    if (tensor->type == kTfLiteInt8 || tensor->type == kTfLiteUInt8) {
        ESP_LOGI(TAG_WW, "%s shape=%s type=%s bytes=%u qscale=%.6f qzero=%d",
                 label, shape, type_name, (unsigned)tensor->bytes,
                 tensor->params.scale, (int)tensor->params.zero_point);
    } else {
        ESP_LOGI(TAG_WW, "%s shape=%s type=%s bytes=%u",
                 label, shape, type_name, (unsigned)tensor->bytes);
    }
}

bool wake_word_engine_init(void)
{
    if (tensors_ready) {
        return true;
    }

    ESP_LOGI(TAG_WW, "Heap pre-TFLM init: %d (min %d)", 
         (int)heap_caps_get_free_size(MALLOC_CAP_8BIT), 
         (int)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));

    const tflite::Model *model = tflite::GetModel(halo_rbot_tflite);
    if (model == nullptr) {
        ESP_LOGE(TAG_WW, "model data missing");
        return false;
    }

    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG_WW, "schema mismatch %d vs %d", model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    // Add common operations (adjust if your model needs others)
    resolver.AddDepthwiseConv2D();
    resolver.AddConv2D();
    resolver.AddAveragePool2D();
    resolver.AddFullyConnected();
    resolver.AddShape(); // Fixes "Didn't find op for builtin opcode 'SHAPE'"
    resolver.AddStridedSlice();
    resolver.AddPack();
    resolver.AddReshape();
    resolver.AddLogistic();
    resolver.AddSoftmax();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddMaxPool2D(); // Fixes "Didn't find op for builtin opcode 'MAX_POOL_2D'"
    resolver.AddMean();      // Fixes "Didn't find op for builtin opcode 'MEAN'"
    resolver.AddAdd();
    resolver.AddSub();
    resolver.AddMul();
    resolver.AddConcatenation();
    resolver.AddPad();
    resolver.AddResizeNearestNeighbor(); 

    interpreter = new (interpreter_buffer) tflite::MicroInterpreter(
        model,
        resolver,
        tensor_arena,
        kTensorArenaSize);

    TfLiteStatus alloc_status = interpreter->AllocateTensors();
    if (alloc_status != kTfLiteOk) {
        ESP_LOGE(TAG_WW, "AllocateTensors() failed (%d)", alloc_status);
        return false;
    }

    input_tensor = interpreter->input(0);
    if (input_tensor == nullptr) {
        ESP_LOGE(TAG_WW, "input tensor missing");
        return false;
    }
    log_tensor_info("input", input_tensor);
    TfLiteTensor *output_tensor = interpreter->output(0);
    log_tensor_info("output", output_tensor);

    tensors_ready = true;
    ESP_LOGI(TAG_WW, "Wake word engine READY (arena=%u bytes)", (unsigned)kTensorArenaSize);
    ESP_LOGI(TAG_WW, "Heap post-TFLM init: %d (min %d)", 
         (int)heap_caps_get_free_size(MALLOC_CAP_8BIT), 
         (int)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    return true;
}

bool wake_word_engine_ready(void)
{
    return tensors_ready;
}

static float dequantize_int(int32_t q, float scale, int32_t zero_point)
{
    return ((float)(q - zero_point)) * scale;
}

static float hz_to_mel(float hz)
{
    return 2595.0f * log10f(1.0f + (hz / 700.0f));
}

static float mel_to_hz(float mel)
{
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

static void init_feature_tables(void)
{
    if (feature_tables_ready) return;

    const float pi = 3.14159265358979f;
    for (int i = 0; i < kWinSamples; i++) {
        hamming_window[i] = 0.54f - 0.46f * cosf((2.0f * pi * i) / (kWinSamples - 1));
    }

    for (int b = 0; b < kFeatureBins; b++) {
        for (int i = 0; i < kFftBins; i++) {
            mel_filters[b][i] = 0.0f;
        }
    }

    const float mel_low = hz_to_mel(0.0f);
    const float mel_high = hz_to_mel(kSampleRate / 2.0f);
    const int mel_points = kFeatureBins + 2;
    float mel_step = (mel_high - mel_low) / (mel_points - 1);
    float mel_edges[kFeatureBins + 2];
    int bin_edges[kFeatureBins + 2];

    for (int i = 0; i < mel_points; i++) {
        mel_edges[i] = mel_low + (mel_step * i);
        float hz = mel_to_hz(mel_edges[i]);
        int bin = (int)floorf((kFftSamples + 1) * hz / kSampleRate);
        if (bin < 0) bin = 0;
        if (bin >= kFftBins) bin = kFftBins - 1;
        bin_edges[i] = bin;
    }

    for (int m = 1; m <= kFeatureBins; m++) {
        int left = bin_edges[m - 1];
        int center = bin_edges[m];
        int right = bin_edges[m + 1];
        if (center == left) center = left + 1;
        if (right == center) right = center + 1;
        if (right > kFftBins) right = kFftBins;

        for (int k = left; k < center && k < kFftBins; k++) {
            mel_filters[m - 1][k] = (float)(k - left) / (float)(center - left);
        }
        for (int k = center; k < right && k < kFftBins; k++) {
            mel_filters[m - 1][k] = (float)(right - k) / (float)(right - center);
        }
    }

    feature_tables_ready = true;
}

static void fft_radix2(float *real, float *imag, int n)
{
    int j = 0;
    for (int i = 1; i < n; i++) {
        int bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if (i < j) {
            float tr = real[i];
            real[i] = real[j];
            real[j] = tr;
            float ti = imag[i];
            imag[i] = imag[j];
            imag[j] = ti;
        }
    }

    const float pi = 3.14159265358979f;
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * pi / (float)len;
        float wlen_r = cosf(ang);
        float wlen_i = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float wr = 1.0f;
            float wi = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int u = i + k;
                int v = u + len / 2;
                float vr = real[v] * wr - imag[v] * wi;
                float vi = real[v] * wi + imag[v] * wr;
                real[v] = real[u] - vr;
                imag[v] = imag[u] - vi;
                real[u] = real[u] + vr;
                imag[u] = imag[u] + vi;
                float next_wr = wr * wlen_r - wi * wlen_i;
                float next_wi = wr * wlen_i + wi * wlen_r;
                wr = next_wr;
                wi = next_wi;
            }
        }
    }
}

bool wake_word_engine_extract_features(const int16_t *pcm, size_t samples, float *feat_out, size_t feat_len)
{
    if (!pcm || !feat_out) return false;
    if (feat_len < (size_t)(kFeatureFrames * kFeatureBins)) return false;
    init_feature_tables();

    static float real[kFftSamples];
    static float imag[kFftSamples];
    static float power[kFftBins];

    for (int frame = 0; frame < kFeatureFrames; frame++) {
        size_t base = (size_t)frame * kHopSamples;
        for (int i = 0; i < kFftSamples; i++) {
            real[i] = 0.0f;
            imag[i] = 0.0f;
        }
        for (int i = 0; i < kWinSamples; i++) {
            size_t idx = base + (size_t)i;
            float sample = 0.0f;
            if (idx < samples) {
                sample = (float)pcm[idx] / 32768.0f;
            }
            real[i] = sample * hamming_window[i];
        }

        fft_radix2(real, imag, kFftSamples);
        for (int i = 0; i < kFftBins; i++) {
            float re = real[i];
            float im = imag[i];
            power[i] = (re * re + im * im) / (float)kFftSamples;
        }

        for (int m = 0; m < kFeatureBins; m++) {
            float sum = 0.0f;
            for (int i = 0; i < kFftBins; i++) {
                sum += power[i] * mel_filters[m][i];
            }
            feat_out[(frame * kFeatureBins) + m] = logf(sum + kLogFloor);
        }
    }
    return true;
}

bool wake_word_engine_infer_from_features(const float *feat_49x10, float *score_out)
{
    if (!tensors_ready || !feat_49x10 || input_tensor == nullptr || interpreter == nullptr) {
        return false;
    }
    if (!input_tensor->dims || input_tensor->dims->size != 3) {
        ESP_LOGE(TAG_WW, "input dims=%d expected 3", input_tensor->dims ? input_tensor->dims->size : -1);
        return false;
    }
    if (input_tensor->dims->data[0] != 1 ||
        input_tensor->dims->data[1] != kFeatureFrames ||
        input_tensor->dims->data[2] != kFeatureBins) {
        ESP_LOGE(TAG_WW, "input shape mismatch [%d,%d,%d]", input_tensor->dims->data[0],
                 input_tensor->dims->data[1], input_tensor->dims->data[2]);
        return false;
    }
    if (input_tensor->type != kTfLiteFloat32) {
        ESP_LOGE(TAG_WW, "input type %d not float32", input_tensor->type);
        return false;
    }

    const size_t input_len = (size_t)(kFeatureFrames * kFeatureBins);
    for (size_t i = 0; i < input_len; i++) {
        input_tensor->data.f[i] = feat_49x10[i];
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG_WW, "Invoke() failed");
        return false;
    }

    TfLiteTensor *output = interpreter->output(0);
    if (!output) {
        ESP_LOGE(TAG_WW, "output missing");
        return false;
    }

    size_t out_len = 0;
    float max_score = -1.0f;
    switch (output->type) {
        case kTfLiteFloat32:
            out_len = output->bytes / sizeof(float);
            for (size_t i = 0; i < out_len; i++) {
                float v = output->data.f[i];
                if (v > max_score) max_score = v;
            }
            break;
        case kTfLiteInt8: {
            out_len = output->bytes / sizeof(int8_t);
            float scale = output->params.scale;
            int32_t zp = output->params.zero_point;
            for (size_t i = 0; i < out_len; i++) {
                float v = dequantize_int(output->data.int8[i], scale, zp);
                if (v > max_score) max_score = v;
            }
            break;
        }
        case kTfLiteUInt8: {
            out_len = output->bytes / sizeof(uint8_t);
            float scale = output->params.scale;
            int32_t zp = output->params.zero_point;
            for (size_t i = 0; i < out_len; i++) {
                float v = dequantize_int(output->data.uint8[i], scale, zp);
                if (v > max_score) max_score = v;
            }
            break;
        }
        case kTfLiteInt16: {
            out_len = output->bytes / sizeof(int16_t);
            float scale = output->params.scale;
            int32_t zp = output->params.zero_point;
            for (size_t i = 0; i < out_len; i++) {
                float v = dequantize_int(output->data.i16[i], scale, zp);
                if (v > max_score) max_score = v;
            }
            break;
        }
        default:
            ESP_LOGE(TAG_WW, "unsupported output type %d", output->type);
            return false;
    }

    if (score_out) *score_out = max_score;
    return true;
}

bool wake_word_engine_infer_from_pcm(const int16_t *pcm, size_t samples, float *score_out)
{
    if (!tensors_ready || !pcm || samples == 0 || input_tensor == nullptr || interpreter == nullptr) {
        return false;
    }
    static bool skip_logged = false;
    if (!input_tensor->dims || input_tensor->dims->size <= 0) {
        return false;
    }
    if (input_tensor->dims->size > 2) {
        if (!skip_logged) {
            ESP_LOGW(TAG_WW, "input dims=%d not PCM; skip infer (need features)", input_tensor->dims->size);
            skip_logged = true;
        }
        return false;
    }

    size_t input_elems = 1;
    for (int i = 0; i < input_tensor->dims->size; i++) {
        int d = input_tensor->dims->data[i];
        if (d <= 0) return false;
        input_elems *= (size_t)d;
    }
    if (input_elems > samples) {
        if (!skip_logged) {
            ESP_LOGW(TAG_WW, "input elems=%u > pcm=%u; skip infer (need features)",
                     (unsigned)input_elems, (unsigned)samples);
            skip_logged = true;
        }
        return false;
    }

    size_t input_len = input_elems;
    switch (input_tensor->type) {
        case kTfLiteInt8:
            break;
        case kTfLiteUInt8:
            break;
        case kTfLiteInt16:
            break;
        case kTfLiteFloat32:
            break;
        default:
            ESP_LOGE(TAG_WW, "unsupported input type %d", input_tensor->type);
            return false;
    }

    if (input_len == 0) {
        ESP_LOGE(TAG_WW, "input_len=0");
        return false;
    }

    size_t step = samples / input_len;
    if (step == 0) step = 1;

    for (size_t i = 0; i < input_len; i++) {
        size_t idx = i * step;
        if (idx >= samples) idx = samples - 1;
        float norm = (float)pcm[idx] / 32768.0f;

        switch (input_tensor->type) {
            case kTfLiteInt8: {
                float scale = input_tensor->params.scale;
                int32_t zp = input_tensor->params.zero_point;
                int32_t q = (int32_t)lrintf(norm / scale) + zp;
                if (q < -128) q = -128;
                if (q > 127) q = 127;
                input_tensor->data.int8[i] = (int8_t)q;
                break;
            }
            case kTfLiteUInt8: {
                float scale = input_tensor->params.scale;
                int32_t zp = input_tensor->params.zero_point;
                int32_t q = (int32_t)lrintf(norm / scale) + zp;
                if (q < 0) q = 0;
                if (q > 255) q = 255;
                input_tensor->data.uint8[i] = (uint8_t)q;
                break;
            }
            case kTfLiteInt16:
                input_tensor->data.i16[i] = (int16_t)(norm * 32767.0f);
                break;
            case kTfLiteFloat32:
                input_tensor->data.f[i] = norm;
                break;
            default:
                break;
        }
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG_WW, "Invoke() failed");
        return false;
    }

    TfLiteTensor *output = interpreter->output(0);
    if (!output) {
        ESP_LOGE(TAG_WW, "output missing");
        return false;
    }

    size_t out_len = 0;
    float max_score = -1.0f;
    switch (output->type) {
        case kTfLiteFloat32:
            out_len = output->bytes / sizeof(float);
            for (size_t i = 0; i < out_len; i++) {
                float v = output->data.f[i];
                if (v > max_score) max_score = v;
            }
            break;
        case kTfLiteInt8: {
            out_len = output->bytes / sizeof(int8_t);
            float scale = output->params.scale;
            int32_t zp = output->params.zero_point;
            for (size_t i = 0; i < out_len; i++) {
                float v = dequantize_int(output->data.int8[i], scale, zp);
                if (v > max_score) max_score = v;
            }
            break;
        }
        case kTfLiteUInt8: {
            out_len = output->bytes / sizeof(uint8_t);
            float scale = output->params.scale;
            int32_t zp = output->params.zero_point;
            for (size_t i = 0; i < out_len; i++) {
                float v = dequantize_int(output->data.uint8[i], scale, zp);
                if (v > max_score) max_score = v;
            }
            break;
        }
        case kTfLiteInt16: {
            out_len = output->bytes / sizeof(int16_t);
            float scale = output->params.scale;
            int32_t zp = output->params.zero_point;
            for (size_t i = 0; i < out_len; i++) {
                float v = dequantize_int(output->data.i16[i], scale, zp);
                if (v > max_score) max_score = v;
            }
            break;
        }
        default:
            ESP_LOGE(TAG_WW, "unsupported output type %d", output->type);
            return false;
    }

    if (score_out) *score_out = max_score;
    return true;
}
