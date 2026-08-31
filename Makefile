CXX ?= c++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O2 -pthread -Iinclude
BUILD_DIR := build
CORE := src/protocol.cpp src/device_registry.cpp

.PHONY: all epoll load smoke test clean

all: $(BUILD_DIR)/edgelink_gateway $(BUILD_DIR)/device_simulator \
	$(BUILD_DIR)/load_generator

epoll: $(BUILD_DIR)/epoll_gateway

load: $(BUILD_DIR)/load_generator

smoke: $(BUILD_DIR)/epoll_gateway $(BUILD_DIR)/load_generator
	./scripts/run_epoll_smoke_test.sh

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/edgelink_gateway: $(CORE) src/gateway_main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/device_simulator: $(CORE) src/device_simulator.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/load_generator: src/protocol.cpp src/load_generator.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/epoll_gateway: src/protocol.cpp src/epoll_gateway_main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/protocol_tests: $(CORE) tests/protocol_tests.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD_DIR)/protocol_tests
	./$(BUILD_DIR)/protocol_tests

clean:
	rm -f $(BUILD_DIR)/edgelink_gateway $(BUILD_DIR)/device_simulator \
		$(BUILD_DIR)/load_generator $(BUILD_DIR)/epoll_gateway \
		$(BUILD_DIR)/protocol_tests
