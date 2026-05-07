.RECIPEPREFIX := >

CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -O3 -fPIC -std=c++17 -Wall -Wextra -fno-exceptions -fno-rtti -fvisibility=hidden -Iinclude
LDFLAGS ?= -shared -Wl,--as-needed
LDLIBS ?= -lm
LV2_CFLAGS := $(shell pkg-config --cflags lv2 2>/dev/null)
LV2_LIBS := $(shell pkg-config --libs lv2 2>/dev/null)

PLUGIN_BUNDLE := vibraphone.lv2
PLUGIN_SO := $(PLUGIN_BUNDLE)/vibraphone.so
BUILD_DIR := build
OBJ := $(BUILD_DIR)/vibraphone.o
SRC := src/vibraphone.cpp

S2400_DIR := s2400-lv2
S2400_TEMPLATE := templates/vibraphone-s2400.ttl.in
URI := https://github.com/AsierT/vibraphone-lv2#vibraphone

all: $(PLUGIN_SO)

$(BUILD_DIR):
>mkdir -p $@

$(PLUGIN_BUNDLE):
>mkdir -p $@

$(OBJ): $(SRC) | $(BUILD_DIR)
>$(CXX) $(CXXFLAGS) $(LV2_CFLAGS) -c $< -o $@

$(PLUGIN_SO): $(OBJ) | $(PLUGIN_BUNDLE)
>$(CC) $< -o $@ $(LDFLAGS) $(LV2_LIBS) $(LDLIBS)

arm64:
>$(MAKE) CXX=aarch64-linux-gnu-g++ CC=aarch64-linux-gnu-gcc CXXFLAGS="$(CXXFLAGS) -march=armv8-a" all

s2400: $(SRC) $(S2400_TEMPLATE)
>mkdir -p $(S2400_DIR)/$(PLUGIN_BUNDLE)
>obj="$(S2400_DIR)/$(PLUGIN_BUNDLE)/vibraphone.o"; \
>binary="vibraphone.so"; \
>ttl="vibraphone.ttl"; \
>$(CXX) $(CXXFLAGS) $(LV2_CFLAGS) -DVIBRAPHONE_INSERT_PORTS -c $(SRC) -o "$$obj"; \
>$(CC) "$$obj" -o "$(S2400_DIR)/$(PLUGIN_BUNDLE)/$$binary" $(LDFLAGS) $(LV2_LIBS) $(LDLIBS); \
>sed -e "s|@URI@|$(URI)|g" -e "s|@BINARY@|$$binary|g" "$(S2400_TEMPLATE)" > "$(S2400_DIR)/$(PLUGIN_BUNDLE)/$$ttl"; \
>printf '@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n<%s>\n    a lv2:Plugin ;\n    lv2:binary <%s> ;\n    rdfs:seeAlso <%s> .\n' "$(URI)" "$$binary" "$$ttl" > "$(S2400_DIR)/$(PLUGIN_BUNDLE)/manifest.ttl"; \
>rm -f "$$obj"

arm64-s2400:
>$(MAKE) CXX=aarch64-linux-gnu-g++ CC=aarch64-linux-gnu-gcc CXXFLAGS="$(CXXFLAGS) -march=armv8-a" s2400

check-s2400:
>file $(S2400_DIR)/$(PLUGIN_BUNDLE)/vibraphone.so
>aarch64-linux-gnu-readelf -d $(S2400_DIR)/$(PLUGIN_BUNDLE)/vibraphone.so | grep NEEDED || readelf -d $(S2400_DIR)/$(PLUGIN_BUNDLE)/vibraphone.so | grep NEEDED || true
>strings -a $(S2400_DIR)/$(PLUGIN_BUNDLE)/vibraphone.so | grep -E 'GLIBC_|GLIBCXX_|GCC_' | sort -V | uniq || true
>aarch64-linux-gnu-nm -D $(S2400_DIR)/$(PLUGIN_BUNDLE)/vibraphone.so | grep lv2_descriptor || nm -D $(S2400_DIR)/$(PLUGIN_BUNDLE)/vibraphone.so | grep lv2_descriptor || true

install: all
>mkdir -p $(HOME)/.lv2/$(PLUGIN_BUNDLE)
>cp -a $(PLUGIN_BUNDLE)/* $(HOME)/.lv2/$(PLUGIN_BUNDLE)/

clean:
>rm -rf $(BUILD_DIR) $(PLUGIN_SO) $(S2400_DIR)

.PHONY: all arm64 s2400 arm64-s2400 check-s2400 install clean
