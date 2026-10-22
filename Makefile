RACK_DIR ?= .deps/Rack-SDK
SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += res LICENSE
include $(RACK_DIR)/plugin.mk

ifdef ARCH_MAC
LDFLAGS += -framework SystemConfiguration -framework CoreFoundation
endif

ifdef ARCH_WIN
# Pull the thread runtime in before GCC adds its own exception/TLS helpers.
LDFLAGS += -static-libgcc -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic -lws2_32 -liphlpapi
endif
