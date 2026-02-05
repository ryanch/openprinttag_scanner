CXX = g++
CXXFLAGS = -std=c++14 -DNATIVE_TEST -Wall -Wextra
INCLUDES = -I ../../include -I ../../src -I .

SRCS = test_app_flow.cpp NativePlatform.cpp
TARGET = test_runner

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
