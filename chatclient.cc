#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>

#include <iostream>
#include <string>

using namespace std;

const int BUF_SIZE = 2000;
int sock = -1;			 // fd for socket

void intHandler(int sig)
{
	signal(sig, SIG_IGN); // Ignore SIGINT
	close(sock);
	exit(1);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "*** Author: Boyi He (boyihe)\n");
		exit(1);
	}

	if (strchr(argv[1], ':') == NULL) {
		fprintf(stderr, "Error: please type in <IP>:<port>\n");
		exit(1);
	}

	// Get IP
	char* ip = strtok(argv[1], ":");
	if (ip == NULL) {
		fprintf(stderr, "Error: invalid IP\n");
		exit(1);
	}

	// Get port
	char* portString = strtok(NULL, ":");
	if (portString == NULL) {
		fprintf(stderr, "Error: invalid port\n");
		exit(1);
	}

	int port = -1;
	try {
		port = stoi(portString, NULL, 10);
	}
	catch (...) {
		fprintf(stderr, "Error: invalid port\n");
		exit(1);
	}

	if (port < 0 || port > 65535) {
		fprintf(stderr, "Error: invalid port\n");
		exit(1);
	}

	// Create a new socket
	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
		exit(1);
	}

	signal(SIGINT, intHandler);   // Establish a handler for SIGING signals (Crtl + C)

	struct sockaddr_in dest;
	bzero(&dest, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	inet_pton(AF_INET, ip, &(dest.sin_addr));


	fd_set readfds;
	string line;

	while (true) {

		FD_ZERO(&readfds);
		FD_SET(STDIN_FILENO, &readfds);
		FD_SET(sock, &readfds);
		int ret = select(sock + 1, &readfds, NULL, NULL, NULL);

		// Send
		if (FD_ISSET(STDIN_FILENO, &readfds)) {

			getline(cin, line);
			if (line == "/quit") {
				close(sock);
				exit(0);
			}
			if (line.size() > BUF_SIZE) {
				cout << "-ERR message exceeds length limit (" << (BUF_SIZE / 2) << ")" << endl;
			} else {
				sendto(sock, line.c_str(), line.size(), 0, (struct sockaddr*)&dest, sizeof(dest));
			}
		}

		// Receive
		if (FD_ISSET(sock, &readfds)) {
			char buf[BUF_SIZE];
			struct sockaddr_in src;
			socklen_t srcSize = sizeof(src);
			int rlen = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&src, &srcSize);
			buf[rlen] = 0;
			cout << buf << endl;
		}
	}

	return 0;
}


