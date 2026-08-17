#
# T113 蓝牙音箱 — 交叉编译 Makefile（镜像 lvgl_demo_build/src/Makefile）
#
# 用法：
#   ./scripts/setup.sh        # 首次：从 SDK 复制 toolchain 到 ./toolchain/
#   make                      # 构建 build/bt_speaker
#   ./scripts/deploy.sh       # adb 部署到板子
#
CC      := $(shell [ -x "$(CURDIR)/toolchain/bin/arm-openwrt-linux-gcc" ] && echo "$(CURDIR)/toolchain/bin/arm-openwrt-linux-gcc" || echo "arm-openwrt-linux-gcc")

# OpenWrt wrapper 编译器要求 STAGING_DIR（app_sdk build.sh 同款做法）
export STAGING_DIR := $(dir $(firstword $(CC)))
LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= $(CURDIR)/third_party

CFLAGS  ?= -std=gnu99 -O2 -g \
	-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard \
	-I$(CURDIR)/src \
	-I$(LVGL_DIR)/ \
	-I$(CURDIR)/third_party/lvgl/src/extra/libs/freetype \
	-I$(CURDIR)/third_party/freetype/include \
	-Wall -Wno-unused-function -Wno-unused-parameter -Wno-missing-prototypes \
	-Wno-sign-compare -Wno-format-nonliteral

# M0 尚未链接 BT 库；M1 起加 -L third_party/bt/lib -lbtmg ...（见 README 里程碑）
LDFLAGS ?= -lm \
	-L$(CURDIR)/third_party/freetype/lib -lfreetype \
	-lz -lbz2 -lpthread -lrt \
	-Wl,-rpath,/usr/lib

BIN  = bt_speaker
BUILD = build

#Collect the files to compile
MAINSRC = ./src/main.c

include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

OBJEXT ?= .o

AOBJS = $(patsubst %,$(BUILD)/%,$(ASRCS:.S=$(OBJEXT)))
COBJS = $(patsubst %,$(BUILD)/%,$(CSRCS:.c=$(OBJEXT)))
MAINOBJ = $(patsubst %,$(BUILD)/%,$(MAINSRC:.c=$(OBJEXT)))

OBJS = $(AOBJS) $(COBJS)

all: $(BUILD)/$(BIN)

$(BUILD)/$(BIN): $(AOBJS) $(COBJS) $(MAINOBJ)
	$(CC) -o $@ $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)
	@echo "===> $@"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

clean:
	rm -rf $(BUILD)

.PHONY: all clean
