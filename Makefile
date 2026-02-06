# -------------------------
# Project Configuration
# -------------------------

TARGET      := eloop
SRC_DIR     := src
BUILD_DIR   := build

# -------------------------
# Compiler
# -------------------------

CC := gcc

# -------------------------
# Flags
# -------------------------

CFLAGS := \
	-Wall -Wextra -Werror \
	-std=c11 -O2 \
	-I$(SRC_DIR)

SAN_FLAGS   := -fsanitize=address -fno-omit-frame-pointer
DEBUG_FLAGS := -g -O0 -DDEBUG

LDFLAGS :=

# -------------------------
# Sources
# -------------------------

SRCS := main.c $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# -------------------------
# Default Target
# -------------------------

all: $(TARGET)

# -------------------------
# Link
# -------------------------

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo "✔ Built $(TARGET)"

# -------------------------
# Compile
# -------------------------

# preserve directories for src files
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# -------------------------
# Debug Build
# -------------------------

debug: CFLAGS  += $(DEBUG_FLAGS) $(SAN_FLAGS)
debug: LDFLAGS += $(SAN_FLAGS)
debug: clean all

# -------------------------
# Clean
# -------------------------

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# -------------------------
# Run
# -------------------------

run: $(TARGET)
	./$(TARGET)

# -------------------------
# Dependencies
# -------------------------

-include $(DEPS)

.PHONY: all clean debug run
