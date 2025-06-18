CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread
BUILD_DIR = build
TARGETS = $(BUILD_DIR)/server $(BUILD_DIR)/tester

.PHONY: all clean test

all: $(BUILD_DIR) $(TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/server: server.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $(BUILD_DIR)/server server.cpp

$(BUILD_DIR)/tester: tester.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $(BUILD_DIR)/tester tester.cpp

clean:
	rm -rf $(BUILD_DIR)

test: $(TARGETS)
	@echo "Starting automated network tests..."
	@mkdir -p results
	@echo "Starting server in background..."
	@./$(BUILD_DIR)/server & echo $$! > server.pid
	@sleep 2
	@echo "Running tester..."
	@./$(BUILD_DIR)/tester
	@echo "Moving results to results folder..."
	@mv log-*.txt results/ 2>/dev/null || true
	@echo "Stopping server..."
	@kill `cat server.pid` 2>/dev/null || true
	@rm -f server.pid
	@echo "Test completed. Results saved to results folder." 