# 默认只编译纯串行模型。并发 API 示例没有自动运行目标。
CC = cc
CXX = c++
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2
BUILD = build/ch01
SERIAL = code/serial/ch01-cache-models/c/demo.c
PARALLEL = code/pthreads/ch01-task-scheduling/demo.c
DISTRIBUTED = code/distributed/ch01-cache-coherence/demo.cpp

.PHONY: all serial test-serial api-check parallel
all: serial

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/cache-c: $(SERIAL) | $(BUILD)
	$(CC) $(CFLAGS) $(SERIAL) -o $@

serial: $(BUILD)/cache-c

test-serial: serial
	$(BUILD)/cache-c --test

# 只进行编译期检查，不链接或执行线程程序。
api-check:
	$(CC) $(CFLAGS) -pthread -fsyntax-only $(PARALLEL)
	$(CXX) $(CXXFLAGS) -pthread -fsyntax-only $(DISTRIBUTED)

# 可选：构建线程示例，但不执行。OpenMP 示例只在文档中介绍。
parallel: $(BUILD)/parallel-c $(BUILD)/distributed-cpp

$(BUILD)/parallel-c: $(PARALLEL) | $(BUILD)
	$(CC) $(CFLAGS) -pthread $(PARALLEL) -o $@

$(BUILD)/distributed-cpp: $(DISTRIBUTED) | $(BUILD)
	$(CXX) $(CXXFLAGS) -pthread $(DISTRIBUTED) -o $@
