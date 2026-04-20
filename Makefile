# Compilers
CXX = g++

# Compiler flags
override CXXFLAGS += -std=c++17

# Linker flags
override LDFLAGS += -flto

# Source files
SRC_FILES = \
	src/vmfoptimizer.cpp

# Installation prefix
PREFIX = /usr/local

TARGET = vmfoptimizer

all: $(TARGET)

$(TARGET): $(SRC_FILES)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

install:
	mkdir -p $(PREFIX)/bin
	cp $(TARGET) $(PREFIX)/bin/

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)
