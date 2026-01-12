CXX = g++
CXXFLAGS = -Wall -Werror -std=c++17 -O2

SRCS = host.cpp conn_fd.cpp conn_pipe.cpp conn_fifo.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = host

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
