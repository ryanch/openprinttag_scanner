CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

all: ha

ha: ha.cpp
	$(CXX) $(CXXFLAGS) -o ha ha.cpp

run: ha
	./ha --help

clean:
	rm -f ha

.PHONY: all run clean
