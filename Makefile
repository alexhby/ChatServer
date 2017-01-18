TARGETS = chatserver chatclient

all: $(TARGETS)

%.o: %.cc
	g++ $^ -c -o $@ -std=c++11

chatserver: chatserver.o
	g++ $^ -o $@ -std=c++11

chatclient: chatclient.o
	g++ $^ -o $@ -std=c++11

clean::
	rm -fv $(TARGETS) *~ *.o
