# Build:   make                    optimised for this machine (-march=native)
#          make ARCH=x86-64-v2     portable x86-64 build
#          make STATIC=1           fully static binary (used by the Docker image)
#          make debug              sanitizer build for testing
# Test:    make test | make test-full | make tactics | make bench
CXX      ?= g++
ARCH     ?= native
EXE      ?= engine
SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:src/%.cpp=build/%.o)

CXXFLAGS := -std=c++17 -Wall -Wextra -O3 -DNDEBUG -funroll-loops -pthread -flto=auto
ifneq ($(ARCH),)
  CXXFLAGS += -march=$(ARCH)
endif
LDFLAGS  := -pthread -flto=auto
ifeq ($(STATIC),1)
  LDFLAGS += -static
endif

ifeq ($(OS),Windows_NT)
  LDLIBS += -lws2_32
  EXE := $(EXE).exe
endif

all: $(EXE)

$(EXE): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

build/%.o: src/%.cpp $(wildcard src/*.h) | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

debug:
	$(MAKE) clean
	$(MAKE) $(EXE) CXXFLAGS="-std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -pthread" 	        LDFLAGS="-pthread -fsanitize=address,undefined"

test: $(EXE)
	./$(EXE) perftsuite tests/perft_suite.epd 5

test-full: $(EXE)
	./$(EXE) perftsuite tests/perft_suite.epd

tactics: $(EXE)
	python3 tests/tactics.py ./$(EXE) 1000

api-test: $(EXE)
	python3 tests/api_test.py ./$(EXE)

bench: $(EXE)
	./$(EXE) bench 13

clean:
	rm -rf build $(EXE) $(EXE).exe

.PHONY: all debug test test-full tactics api-test bench clean
