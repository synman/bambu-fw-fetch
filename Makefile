CXX      = clang++
CXXFLAGS = -std=c++17 -Wall -O2
TARGET   = bambu_fw_fetch
SRC      = bambu_fw_fetch.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
