CXX ?= c++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O2 -pthread -Iinclude
BUILD_DIR := build
CORE := src/protocol.cpp src/device_registry.cpp

.PHONY: all test clean

all: $(BUILD_DIR)/edgelink_gateway $(BUILD_DIR)/device_simulator

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/edgelink_gateway: $(CORE) src/gateway_main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/device_simulator: $(CORE) src/device_simulator.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/protocol_tests: $(CORE) tests/protocol_tests.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD_DIR)/protocol_tests
	./$(BUILD_DIR)/protocol_tests

clean:
	rm -f $(BUILD_DIR)/edgelink_gateway $(BUILD_DIR)/device_simulator $(BUILD_DIR)/protocol_tests

