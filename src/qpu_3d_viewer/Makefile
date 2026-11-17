CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra
LDFLAGS ?= -lX11 -lGL -lm -lpthread

OUT = ../../bin/omniq-qpu-3d

all: $(OUT)

$(OUT): main.cpp
	mkdir -p ../../bin
	$(CXX) $(CXXFLAGS) -o $(OUT) main.cpp $(LDFLAGS)

clean:
	rm -f $(OUT)

.PHONY: all clean
