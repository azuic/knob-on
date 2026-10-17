RACK_DIR ?= .deps/Rack-SDK
SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += res LICENSE
include $(RACK_DIR)/plugin.mk

ifdef ARCH_MAC
LDFLAGS += -framework SystemConfiguration -framework CoreFoundation
endif
