# Bot mau HEXUDON — C++17, transport HTTP (raw socket, khong thu vien ngoai).
# Linux/macOS: khong can lib them. Windows (MinGW g++): make LDLIBS=-lws2_32 (MSVC tu link).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDLIBS   ?=

.PHONY: all clean
all: bot

bot: main.cpp http.hpp minijson.hpp
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(LDLIBS)

clean:
	rm -f bot bot.exe
