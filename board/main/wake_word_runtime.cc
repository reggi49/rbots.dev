#include "wake_word_runtime.h"

#include <new>

#include "esp_log.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "wake_word_model.h"

namespace {

constexpr size_t kTensorArenaSize = 30 * 1024;

alignas(16) static uint8_t tensor_arena[kTensorArenaSize];
alignas(tflite::MicroInterpreter) static uint8_t interpreter_buffer[sizeof(tflite::MicroInterpreter)];

static tflite::MicroMutableOpResolver<25> resolver;
static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input_tensor = nullptr;
static bool tensors_ready = false;

} // namespace

static const char *TAG_WW = "WW";

bool wake_word_engine_init(void)
{
    if (tensors_ready) {
        return true;
    }

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

    tensors_ready = true;
    ESP_LOGI(TAG_WW, "Wake word engine READY (arena=%u bytes)", (unsigned)kTensorArenaSize);
    return true;
}

bool wake_word_engine_ready(void)
{
    return tensors_ready;
}

