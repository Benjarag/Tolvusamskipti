CXX = g++
CXXFLAGS = -Wall -g -std=c++11
# For C use: CC = gcc, CFLAGS = -Wall -g -std=c99

SERVER_TARGET = tsamgroup21
CLIENT_TARGET = tsamclient
SRCDIR = src
SERVER_SRC = $(SRCDIR)/server.cpp
CLIENT_SRC = $(SRCDIR)/client.cpp

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) -o $(SERVER_TARGET) $(SERVER_SRC)

$(CLIENT_TARGET): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -o $(CLIENT_TARGET) $(CLIENT_SRC)

clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)

.PHONY: all clean

