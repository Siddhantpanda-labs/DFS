CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = 

# Directories
COORDINATOR_DIR = coordinator
NODE_DIR = node
CLIENT_DIR = client

# Source files
COORDINATOR_SRC = $(COORDINATOR_DIR)/coordinator.cpp
NODE_SRC = $(NODE_DIR)/node.cpp
CLIENT_SRC = $(CLIENT_DIR)/client.cpp

# Executables
COORDINATOR_EXE = coordinator
NODE_EXE = node
CLIENT_EXE = client

.PHONY: all clean coordinator node client

all: coordinator node client

coordinator: $(COORDINATOR_EXE)

node: $(NODE_EXE)

client: $(CLIENT_EXE)

$(COORDINATOR_EXE): $(COORDINATOR_SRC)
	$(CXX) $(CXXFLAGS) -o $(COORDINATOR_EXE) $(COORDINATOR_SRC) $(LDFLAGS)
	@echo "Built $(COORDINATOR_EXE)"

$(NODE_EXE): $(NODE_SRC)
	$(CXX) $(CXXFLAGS) -o $(NODE_EXE) $(NODE_SRC) $(LDFLAGS)
	@echo "Built $(NODE_EXE)"

$(CLIENT_EXE): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -o $(CLIENT_EXE) $(CLIENT_SRC) $(LDFLAGS)
	@echo "Built $(CLIENT_EXE)"

clean:
	rm -f $(COORDINATOR_EXE) $(NODE_EXE) $(CLIENT_EXE)
	@echo "Cleaned executables"

