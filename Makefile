# ═══════════════════════════════════════════════════════════════════════════════
# KRYPTOS-VX v3.0.0 "Apex" — Zero-Dependency C++20 Makefile
# ═══════════════════════════════════════════════════════════════════════════════

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O3 -mavx2 -mfma -pthread -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS  ?= -pthread

CORE_SRCS := \
	src/core/kernel_fs.cpp \
	src/core/simd_scanner.cpp \
	src/core/soa_index.cpp \
	src/engine/adb_bridge.cpp \
	src/engine/binary_profiler.cpp \
	src/engine/context_packer.cpp \
	src/engine/debloater.cpp \
	src/engine/duplicate_finder.cpp \
	src/engine/code_auditor.cpp \
	src/engine/fastcdc_sync.cpp \
	src/engine/mcp_server.cpp \
	src/engine/trigram_watcher.cpp \
	src/ui/tui_dashboard.cpp

MAIN_SRC  := src/main.cpp
TEST_SRC  := tests/test_main.cpp
BENCH_SRC := benchmarks/bench_main.cpp

BIN_DIR   := bin
TARGET    := $(BIN_DIR)/kryptos_vx
WIN_EXE   := $(BIN_DIR)/kryptos_vx.exe
TEST_BIN  := $(BIN_DIR)/kryptos_vx_tests
BENCH_BIN := $(BIN_DIR)/kryptos_vx_bench

.PHONY: all clean test bench sanitize

all: $(TARGET) $(TEST_BIN) $(BENCH_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(TARGET): $(MAIN_SRC) $(CORE_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(MAIN_SRC) $(CORE_SRCS) -o $(TARGET) $(LDFLAGS)
	cp -f $(TARGET) $(WIN_EXE)

$(TEST_BIN): $(TEST_SRC) $(CORE_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) $(CORE_SRCS) -o $(TEST_BIN) $(LDFLAGS)

$(BENCH_BIN): $(BENCH_SRC) $(CORE_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(BENCH_SRC) $(CORE_SRCS) -o $(BENCH_BIN) $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

sanitize: $(MAIN_SRC) $(TEST_SRC) $(CORE_SRCS) | $(BIN_DIR)
	$(CXX) -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Iinclude $(TEST_SRC) $(CORE_SRCS) -o $(BIN_DIR)/kryptos_vx_tests_asan
	./$(BIN_DIR)/kryptos_vx_tests_asan

clean:
	rm -rf $(BIN_DIR)/* build/
