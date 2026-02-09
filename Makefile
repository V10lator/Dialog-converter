TARGET_EXEC := diagConv
VERSION := 0.0.1

CFLAGS := 	-march=x86-64-v2 -O3 -ffast-math -fno-plt -Wall -Wextra \
		-flto=4 -fno-fat-lto-objects -fuse-linker-plugin \
		-floop-interchange -ftree-loop-distribution -floop-strip-mine -floop-block \
		-fgraphite-identity -floop-nest-optimize -floop-parallelize-all -ftree-parallelize-loops=4 -ftree-vectorize \
		-fipa-pta -fno-semantic-interposition -fno-common -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -Wall -pipe

LDFLAGS := 	-Wl,-O1 -Wl,--sort-common -Wl,--as-needed -Wl,-z,relro -Wl,-z,now \
		-Wl,-z,pack-relative-relocs -Wl,--hash-style=gnu

BUILD_DIR := ./build
SRC_DIRS := ./
INCLUDE_DIRS := ./

SRCS := $(shell find $(SRC_DIRS) -name "*.c")
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)
INC_FLAGS := $(addprefix -I,$(INCLUDE_DIRS))

./$(TARGET_EXEC): $(OBJS)
	gcc $(CFLAGS) $(INC_FLAGS) $(OBJS) -o $@ $(LDFLAGS)
	sstrip -z $@

$(BUILD_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	gcc $(CFLAGS) $(INC_FLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(TARGET_EXEC) $(BUILD_DIR)
