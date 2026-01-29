file(GLOB_RECURSE MODEL_SOURCES "model-parameters/*.cpp" "tflite-model/*.cpp")
file(GLOB_RECURSE SDK_SOURCES 
    "edge-impulse-sdk/classifier/*.cpp" 
    "edge-impulse-sdk/dsp/*.cpp"
    "edge-impulse-sdk/dsp/*.c"
    "edge-impulse-sdk/dsp/kissfft/*.cpp"
    "edge-impulse-sdk/tensorflow/*.c"
    "edge-impulse-sdk/tensorflow/*.cpp"
    "edge-impulse-sdk/tensorflow/*.cc"
)
list(FILTER SDK_SOURCES EXCLUDE REGEX ".*_test\\.cc$")
list(FILTER SDK_SOURCES EXCLUDE REGEX ".*_test\\.cpp$")
list(FILTER SDK_SOURCES EXCLUDE REGEX ".*_test_helpers\\.cc$")
list(FILTER SDK_SOURCES EXCLUDE REGEX ".*debug_log_test\\.cpp$")

file(GLOB_RECURSE PORTING_SOURCES "edge-impulse-sdk/porting/espressif/*.c" "edge-impulse-sdk/porting/espressif/*.cpp")

idf_component_register(
    SRCS 
        ${MODEL_SOURCES} 
        ${SDK_SOURCES} 
        ${PORTING_SOURCES}
    INCLUDE_DIRS
        "."
        "edge-impulse-sdk"
        "model-parameters"
        "tflite-model"
        "edge-impulse-sdk/porting/espressif/esp-dsp/modules/common/include"
        "edge-impulse-sdk/porting/espressif/esp-dsp/modules/fft/include"
    REQUIRES
        json
        driver
        esp_timer
        log
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE 
    TF_LITE_STATIC_MEMORY=1
    EI_CLASSIFIER_SOFTWARE_FFT=1
    EI_DSP_USE_INT64_FFT=1
    EI_CLASSIFIER_TFLITE_ENABLE_ARC=0
    EIDSP_INCLUDE_KISSFFT=1
    EIDSP_USE_ESP_DSP=0
    EIDSP_USE_CMSIS_DSP=0
    EIDSP_USE_CEVA_DSP=0
    EI_PORTING_ESPRESSIF=1
)
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -std=gnu++11
    -w
    -fpermissive
    -Wno-error
)
