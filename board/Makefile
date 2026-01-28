MODULE := board

# Add source directories to include path
CFLAGS += -I$(COMPONENT_PATH)/include
CFLAGS += -I$(COMPONENT_PATH)/main

# Add new source files
COMPONENT_SRCS += src/board_display.c \
                  src/board_network.c \
                  src/board_power.c \
                  src/board_audio.c \
                  main/board.c \
                  main/audio_player.c \
                  main/cloud_client.c \
                  main/tts_client.c \
                  main/wake_word_model.cc \
                  main/wake_word_runtime.cc \
                  main/command_dispatcher.c

# Include the component makefile logic
include $(IDF_PATH)/make/component_common.mk
