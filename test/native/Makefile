CXX = g++
CC = gcc
CXXFLAGS = -std=c++14 -DNATIVE_TEST -Wall -Wextra
CFLAGS = -std=c11 -DNATIVE_TEST -Wall -Wextra
INCLUDES = -I ../../include -I ../../src -I . -I ../../lib/openprinttag
JSON_INCLUDES = -I ../../.pio/libdeps/esp32dev/htcw_json/src -I ../../.pio/libdeps/esp32dev/htcw_io/src -I ../../.pio/libdeps/esp32dev/htcw_bits

# OpenPrintTag library sources (compiled as C)
OPT_C_SRCS = ../../lib/openprinttag/openprinttag_lib.c ../../lib/openprinttag/cbor_native.c
OPT_OBJS = openprinttag_lib.o cbor_native.o

# App flow tests (uses TestNFCManager stub)
COMMON_NATIVE_SRCS = NativePlatform.cpp

APP_FLOW_SRCS = test_app_flow.cpp $(COMMON_NATIVE_SRCS)
APP_FLOW_TARGET = test_runner

# NFC read tests (uses real NFCManager with StubNFCConnection)
NFC_READ_SRCS = test_nfc_read.cpp $(COMMON_NATIVE_SRCS)
NFC_READ_TARGET = test_nfc_read_runner

# Bgcode parser tests
BGCODE_SRCS = test_bgcode_parser.cpp $(COMMON_NATIVE_SRCS)
BGCODE_TARGET = test_bgcode_parser

LCD_MANAGER_SRCS = test_lcd_manager.cpp $(COMMON_NATIVE_SRCS)
LCD_MANAGER_TARGET = test_lcd_manager_runner

JSON_PULL_HELPERS_SRCS = test_json_pull_helpers.cpp ../../src/JsonPullHelpers.cpp ../../.pio/libdeps/esp32dev/htcw_io/src/source/io_stream.cpp
JSON_PULL_HELPERS_TARGET = test_json_pull_helpers_runner

# Raw write tests (uses real NFCManager with StubNFCConnection)
RAW_WRITE_SRCS = test_raw_write.cpp $(COMMON_NATIVE_SRCS)
RAW_WRITE_TARGET = test_raw_write_runner

all: $(APP_FLOW_TARGET) $(NFC_READ_TARGET) $(BGCODE_TARGET) $(LCD_MANAGER_TARGET) $(JSON_PULL_HELPERS_TARGET) $(RAW_WRITE_TARGET)
	./$(APP_FLOW_TARGET)
	./$(NFC_READ_TARGET)
	./$(BGCODE_TARGET)
	./$(LCD_MANAGER_TARGET)
	./$(JSON_PULL_HELPERS_TARGET)
	./$(RAW_WRITE_TARGET)

# Compile C library objects
openprinttag_lib.o: ../../lib/openprinttag/openprinttag_lib.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

cbor_native.o: ../../lib/openprinttag/cbor_native.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(APP_FLOW_TARGET): $(APP_FLOW_SRCS) $(OPT_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(APP_FLOW_SRCS) $(OPT_OBJS) -o $(APP_FLOW_TARGET)

$(NFC_READ_TARGET): $(NFC_READ_SRCS) $(OPT_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(NFC_READ_SRCS) $(OPT_OBJS) -o $(NFC_READ_TARGET)

$(BGCODE_TARGET): $(BGCODE_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(BGCODE_SRCS) -o $(BGCODE_TARGET)

$(LCD_MANAGER_TARGET): $(LCD_MANAGER_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LCD_MANAGER_SRCS) -o $(LCD_MANAGER_TARGET)

$(JSON_PULL_HELPERS_TARGET): $(JSON_PULL_HELPERS_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(JSON_INCLUDES) $(JSON_PULL_HELPERS_SRCS) -o $(JSON_PULL_HELPERS_TARGET)

$(RAW_WRITE_TARGET): $(RAW_WRITE_SRCS) $(OPT_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(RAW_WRITE_SRCS) $(OPT_OBJS) -o $(RAW_WRITE_TARGET)

run-app-flow: $(APP_FLOW_TARGET)
	./$(APP_FLOW_TARGET)

run-nfc-read: $(NFC_READ_TARGET)
	./$(NFC_READ_TARGET)

run-bgcode: $(BGCODE_TARGET)
	./$(BGCODE_TARGET)

run-lcd-manager: $(LCD_MANAGER_TARGET)
	./$(LCD_MANAGER_TARGET)

run-json-pull-helpers: $(JSON_PULL_HELPERS_TARGET)
	./$(JSON_PULL_HELPERS_TARGET)

run-raw-write: $(RAW_WRITE_TARGET)
	./$(RAW_WRITE_TARGET)

clean:
	rm -f $(APP_FLOW_TARGET) $(NFC_READ_TARGET) $(BGCODE_TARGET) $(LCD_MANAGER_TARGET) $(JSON_PULL_HELPERS_TARGET) $(RAW_WRITE_TARGET) $(OPT_OBJS)

.PHONY: all clean run-app-flow run-nfc-read run-bgcode run-lcd-manager run-json-pull-helpers run-raw-write
