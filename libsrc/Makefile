CC = gcc 
CFLAGS = $(shell pkg-config --cflags libxma2api libxma2plugin xvbm libxrm)
INCLUDE_DIR = ../include/
CFLAGS += -Wall -O0 -g -fPIC -shared -std=gnu99
CFLAGS += -I$(INCLUDE_DIR)
LDFLAGS = $(shell pkg-config --libs libxma2api libxma2plugin xvbm libxrm)

TARGET = libu30_xma_codec.so

BUILD_DIR  := ../build
SRC_DIR    := src
# SRC_DIR    := .
OBJ_DIR    := obj
SRCS := $(wildcard $(SRC_DIR)/xlnx_encoder_app.c)
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

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/$(TARGET)