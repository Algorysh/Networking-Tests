CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread
TARGETS = server tester

.PHONY: all clean

all: $(TARGETS)

server: server.cpp
	$(CXX) $(CXXFLAGS) -o server server.cpp

tester: tester.cpp
	$(CXX) $(CXXFLAGS) -o tester tester.cpp

clean:
	rm -f $(TARGETS)

test: server tester
	@echo "To run the tests:"
	@echo "1. Start the server in one terminal: ./server"
	@echo "2. Run the tester in another terminal: ./tester" 