#---------------------------------------------------------------------------------
# Genesis Plus GX ULTIMATE - Wii U port: USB storage (FAT) for the app
#   libfat  (wiiu-env/libfat, BSD-style license, see the source files)
#   libmocha (wiiu-env/libmocha, LGPL 3, see libmocha/LICENSE): raw USB access
#   through the Mocha payload of Aroma
# Built with wut only (none of the libogc headers of the app).
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment.")
endif
include $(DEVKITPRO)/wut/share/wut_rules

OUT     := libwiiuusb.a
OBJDIR  := obj
SRC_C   := $(wildcard libfat/source/*.c) $(wildcard libmocha/source/*.c)
SRC_CPP := $(wildcard libmocha/source/*.cpp) $(wildcard libmocha/source/devoptab/*.cpp)
OBJS    := $(patsubst %,$(OBJDIR)/%.o,$(SRC_C) $(SRC_CPP))

FLAGS   := -O2 -ffunction-sections -fdata-sections $(MACHDEP) -D__WIIU__ -D__WUT__ \
           -I$(CURDIR)/libfat/include -I$(CURDIR)/libmocha/include -I$(CURDIR)/libmocha/source \
           -I$(DEVKITPRO)/wut/include \
           -DfatInitDefault=libfat_fatInitDefault -DfatInit=libfat_fatInit
CFLAGS   := $(FLAGS) -std=gnu11
CXXFLAGS := $(FLAGS) -std=gnu++20 -fno-exceptions -fno-rtti

all: $(OUT)

$(OUT): $(OBJS)
	@rm -f $@
	@$(AR) rcs $@ $^
	@echo built ... $@

$(OBJDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@rm -rf $(OBJDIR) $(OUT)
