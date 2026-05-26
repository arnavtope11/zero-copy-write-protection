CXX      := g++
CXXFLAGS  := -std=c++17 -O3 -Iinclude -march=native

COMMON_SRC := $(wildcard allocator/*.cpp user/*.cpp)
COMMON_OBJ := $(COMMON_SRC:.cpp=.o)

TESTS      := test_allocator test_in_flight
TEST_SRC   := $(addsuffix .cpp,  $(addprefix test/,$(TESTS)))
TEST_OBJ   := $(TEST_SRC:.cpp=.o)

BENCH_SRC   := bench_allocator.cpp
BENCH_OBJ   := $(BENCH_SRC:.cpp=.o)
BENCH_BIN   := bench_allocator

BINARIES := $(TESTS) $(BENCH_BIN)

all: $(BINARIES)

$(TESTS): %: $(COMMON_OBJ) test/%.o
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BENCH_BIN): $(COMMON_OBJ) $(BENCH_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(COMMON_OBJ) $(TEST_OBJ) $(BENCH_OBJ) $(BINARIES)

