CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -pthread
SOURCES = server.cpp client.cpp protocol.cpp networking.cpp
SERVER_OBJ = server.o protocol.o networking.o
CLIENT_OBJ = client.o protocol.o networking.o
TARGET_SERVER = tsamgroup29
TARGET_CLIENT = tsamclient

all: $(TARGET_SERVER) $(TARGET_CLIENT)

$(TARGET_SERVER): $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET_SERVER) $(SERVER_OBJ)

$(TARGET_CLIENT): $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET_CLIENT) $(CLIENT_OBJ)

server.o: server.cpp protocol.h networking.h
	$(CXX) $(CXXFLAGS) -c server.cpp

client.o: client.cpp protocol.h networking.h
	$(CXX) $(CXXFLAGS) -c client.cpp

protocol.o: protocol.cpp protocol.h
	$(CXX) $(CXXFLAGS) -c protocol.cpp

networking.o: networking.cpp networking.h protocol.h
	$(CXX) $(CXXFLAGS) -c networking.cpp

clean:
	rm -f *.o $(TARGET_SERVER) $(TARGET_CLIENT)

.PHONY: all clean