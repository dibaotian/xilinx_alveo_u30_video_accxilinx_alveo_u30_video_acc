CC = gcc 
INCLUDE_DIR = ../include/
INCLUDE_DIR2 = /opt/xilinx/ffmpeg/include
CFLAGS += -Wall -O0 -g -std=gnu99
CFLAGS += -I$(INCLUDE_DIR)
CFLAGS += -I$(INCLUDE_DIR2)
# LDFLAGS =-L/opt/xilinx/xrt/lib -lxma2api -L/opt/xilinx/xvbm/lib -lxvbm
LDFLAGS += -L../build -lu30_xma_codec
LDFLAGS += -L/opt/xilinx/ffmpeg/lib/ -lavdevice -lavformat -lavcodec -lavfilter -lswresample -lswscale
# LDFLAGS += -L/opt/xilinx/ffmpeg/lib
# LDFLAGS =-lxma2api -lxma2plugin -lxvbm -lxrm -lu30_xma_encode 

TARGET = u30_xma_enc

BUILD_DIR  := .
SRC_DIR    := .
OBJ_DIR    := obj
SRCS := $(wildcard $(SRC_DIR)/main.c)
OBJS :=  $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/$(OBJ_DIR)/%.o)

ifeq ($(VERSION), V2)
CFLAGS += -DU30V2
endif

.PHONY: all
all: $(BUILD_DIR)/${TARGET}

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): $(BUILD_DIR)/$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c 
	@mkdir -p $(BUILD_DIR)/$(OBJ_DIR)
	$(CC) -c $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "!!![build done]!!!"

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/$(TARGET)
	rm -rf $(OBJS)
