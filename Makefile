TARGETS = chatserver chatclient

all: $(TARGETS)

%.o: %.cc
	g++ $^ -c -o $@ -std=c++11

chatserver: chatserver.o
	g++ $^ -o $@ -std=c++11

chatclient: chatclient.o
	g++ $^ -o $@ -std=c++11

pack:
	rm -f submit-hw3.zip
	zip -r submit-hw3.zip README Makefile *.c* *.h*

clean::
	rm -fv $(TARGETS) *~ *.o submit-hw3.zip

realclean:: clean
	rm -fv cis505-hw3.zip
