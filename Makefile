# Makefile for UART Service Application
# Target: Linux version of the embedded UART-to-network service

# Compiler settings
CC = arm-linux-gnueabihf-gcc
#CFLAGS = -Wall -Wextra -std=c99 -D_LINUX_ -D_REENTRANT -D_GNU_SOURCE -pthread
CFLAGS = -Wall -Wextra -std=c99  -D_REENTRANT -D_GNU_SOURCE -pthread
CFLAGS += -I./HAL -I./APP/inc -I./APP/screen -I./APP/report -I./APP/core -I./main

# Build directory for intermediate files
BUILD_DIR = build

# Output directory for executable
OUTPUT_DIR = bin

# Debug/Release configuration
DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# Source directories
HAL_DIR = HAL
APP_DIR = APP
APP_CORE_DIR = APP/core
APP_INC_DIR = APP/inc
APP_REPORT_DIR = APP/report
APP_SCREEN_DIR = APP/screen
MAIN_DIR = main

# HAL source files
HAL_SOURCES = \
    $(HAL_DIR)/hal_com.c \
    $(HAL_DIR)/hal_flash.c \
    $(HAL_DIR)/hal_log.c \
    $(HAL_DIR)/hal_ringbuffer.c \
    $(HAL_DIR)/hal_timer.c \
    $(HAL_DIR)/hal_axi16550.c \
    $(HAL_DIR)/screen_uart.c


# APP source files (core modules)
APP_SOURCES = \
    $(APP_CORE_DIR)/app_dev.c \
    $(APP_CORE_DIR)/app_init.c \
    $(APP_CORE_DIR)/app_net_cfg.c \
    $(APP_CORE_DIR)/app_net_con.c \
    $(APP_CORE_DIR)/app_net_proto.c \
    $(APP_CORE_DIR)/app_net_scheduler.c \
    $(APP_CORE_DIR)/app_realtime.c \
    $(APP_CORE_DIR)/app_crash_handler.c \
    $(APP_CORE_DIR)/app_resource_init.c \
    $(APP_CORE_DIR)/app_task_creation.c \
    $(APP_CORE_DIR)/app_uart.c \
    $(APP_CORE_DIR)/app_udp_search.c \
    $(APP_CORE_DIR)/app_update.c \
    $(APP_CORE_DIR)/app_update_env.c \
    $(APP_CORE_DIR)/app_watchdog.c

# Report source files
REPORT_SOURCES = \
    $(APP_REPORT_DIR)/app_ports.c \
    $(APP_REPORT_DIR)/app_report.c \
    $(APP_REPORT_DIR)/app_report_basic.c \
    $(APP_REPORT_DIR)/app_report_management.c \
    $(APP_REPORT_DIR)/app_report_mode.c \
    $(APP_REPORT_DIR)/app_report_network.c \
    $(APP_REPORT_DIR)/app_report_serial.c \
    $(APP_REPORT_DIR)/app_report_status.c

# Screen source files (if needed)
SCREEN_SOURCES = \
    $(APP_SCREEN_DIR)/key.c \
    $(APP_SCREEN_DIR)/my_screen_driver.c \
    $(APP_SCREEN_DIR)/ui_menu_system.c \
    $(APP_SCREEN_DIR)/ui_state_device_info.c \
    $(APP_SCREEN_DIR)/ui_state_factory_reset.c \
    $(APP_SCREEN_DIR)/ui_state_lock.c \
    $(APP_SCREEN_DIR)/ui_state_main.c \
    $(APP_SCREEN_DIR)/ui_state_menu_list.c \
    $(APP_SCREEN_DIR)/ui_state_network.c \
    $(APP_SCREEN_DIR)/ui_state_restart.c

# System abstraction layer
SAL_SOURCES = \
    $(MAIN_DIR)/system_abstraction_layer.c

# Combine all sources
SOURCES = $(HAL_SOURCES) $(APP_SOURCES) $(REPORT_SOURCES) $(SCREEN_SOURCES) $(SAL_SOURCES) $(MAIN_DIR)/init_app_nport.c

# Object files - output to build directory while preserving directory structure
OBJECTS = $(addprefix $(BUILD_DIR)/,$(SOURCES:.c=.o))

# Target executable name
TARGET = $(OUTPUT_DIR)/uart_service

# Default target
.PHONY: all
all: $(TARGET)

# Ensure build directory exists (按需创建子目录)
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/HAL $(BUILD_DIR)/APP/core $(BUILD_DIR)/APP/report $(BUILD_DIR)/APP/screen $(BUILD_DIR)/main

# Ensure output directory exists
$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

# Link the target executable
$(TARGET): $(OBJECTS) | $(OUTPUT_DIR)
	$(CC) $(OBJECTS) -o $@ -lpthread -lm -lrt
	@echo "Build completed: $(TARGET)"

# Compile C source files to object files in build directory
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(OUTPUT_DIR)/uart_service
	@echo "Clean completed"

# Rebuild everything
.PHONY: rebuild
rebuild: clean all

# Install target (if needed)
.PHONY: install
install: $(TARGET)
	@echo "Installing $(TARGET)..."
	@cp $(TARGET) /usr/local/bin/ 2>/dev/null || echo "Please copy $(TARGET) to your desired installation directory"

# Print build information
.PHONY: info
info:
	@echo "Build configuration:"
	@echo "  CC = $(CC)"
	@echo "  CFLAGS = $(CFLAGS)"
	@echo "  TARGET = $(TARGET)"
	@echo "  SOURCES = $(SOURCES)"

# Run the application (after building)
.PHONY: run
run: $(TARGET)
	./$(OUTPUT_DIR)/uart_service

# Check dependencies
.PHONY: check
check:
	@echo "Checking for required tools..."
	@which $(CC) > /dev/null || (echo "Error: $(CC) not found"; exit 1)
	@echo "Compiler $(CC) found"
	@echo "Build environment OK"

# Clean additional test objects
.PHONY: clean_test
clean_test:
	rm -f $(BUILD_DIR)/test_uart_main.o uart_test 2>/dev/null
	@echo "Test objects cleaned"