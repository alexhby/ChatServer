#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

#include <iostream>
#include <string>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <vector>
#include <algorithm>
#include <utility>
#include <array>


using namespace std;

const int UNORDERED = 0;
const int FIFO = 1;
const int TOTAL = 2;
const int BUF_SIZE = 2000;			// line buffer size
const int MAX_ROOM = 20;			// Max number of chat rooms

bool debugMode = false;
int order = UNORDERED;				// ordering mode
int sIndex = 0;						// server index
int sock = -1;						// client socket
unordered_map<int, struct sockaddr_in> forwardAddrs;		// server index => forward addresses
unordered_map<string, struct sockaddr_in> rooms[MAX_ROOM];	// roomID maps to clientID & client addr
unordered_map<string, int> roomMap;         				// ip:port => room numbers
unordered_map<string, string> nickMap;      				// ip:port => nick names


void intHandler(int sig)
{
	signal(sig, SIG_IGN); // Ignore SIGINT
	close(sock);
	exit(0);
}

string getTime() {					// return timestamp for debug mode
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	char buff[100];
	strftime(buff, sizeof buff, "%T.", localtime(&ts.tv_sec));
	return string(buff) + to_string(ts.tv_nsec / 1000); // add microsecends
}

void deliver (int roomNum, const char *msg) {
	for (auto roomIter = rooms[roomNum - 1].begin(); roomIter != rooms[roomNum - 1].end(); roomIter++) {
		sendto(sock, msg, strlen(msg), 0, (struct sockaddr*) & (roomIter->second), sizeof(roomIter->second));
	}
}

void multicast (const char *msg) {
	for (auto servIter = forwardAddrs.begin(); servIter != forwardAddrs.end(); servIter++) {
		if (servIter->first != sIndex)
			sendto(sock, msg, strlen(msg), 0, (struct sockaddr*) & (servIter->second), sizeof(servIter->second));
	}
}




int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "*** Author: Boyi He (boyihe)\n");
		exit(1);
	}

	int argIndex = 1;				// index of argument after opt
	int opt;
	while ((opt = getopt(argc, argv, "vo:")) != -1)
	{
		switch (opt) {

		case 'v':	// Debug mode on
			debugMode = true;
			argIndex++;
			continue;

		case 'o': 	// Set ordering mode
			if (strcmp(optarg, "unordered") == 0) {
				order = UNORDERED;
			} else if (strcmp(optarg, "fifo") == 0) {
				order = FIFO;
			} else if (strcmp(optarg, "total") == 0) {
				order = TOTAL;
			} else {
				cerr << "Error: Invalid ordering mode (unordered/fifo/total)" << endl;
				exit(1);
			}
			argIndex += 2;
			continue;

		default: /* '?' */
			cerr << "Error: Invalid optional argument" << endl;
			exit(1);
		}
	}

	if (argIndex != argc - 2) {
		cerr << "Error: Require two arguments (file addr, index)" << endl;
		exit(1);
	}


	try {	// Get server index
		sIndex = stoi(argv[argc - 1], NULL, 10);
	}
	catch (...) {
		cerr << "Error: Invalid server index" << endl;
		exit(1);
	}

	if (sIndex < 1) {
		cerr << "Error: Invalid server index" << endl;
		exit(1);
	}


	ifstream ifs(argv[argc - 2]);
	if (!ifs.is_open()) {
		cerr << "Error: Fail to open the address file\n";
		exit(1);
	}


	// Read all ip:port from addr file
	vector<string> forwardIPs;
	vector<int> forwardPorts;
	string myBindIP;
	int myBindPort;

	int lineIndex = 0;
	string line;
	while (getline(ifs, line)) {
		lineIndex++;

		string forward = line.substr(0, line.find(','));
		forwardIPs.push_back( forward.substr(0, forward.find(':')) );	// Get a forward IP

		size_t found = line.find(':');
		if (found == string::npos) {
			cerr << "Error: Invalid forward address at index " << lineIndex << endl;
			exit(1);
		}
		try {															// Get a forward port
			forwardPorts.push_back( stoi(forward.substr(forward.find(':') + 1), NULL, 10) );
		}
		catch (...) {
			cerr << "Error: Invalid forward port at index " << lineIndex << endl;
			exit(1);
		}
		if (forwardPorts.back() < 1 || forwardPorts.back() > 65535) {
			cerr << "Error: Invalid forward port at index " << lineIndex << endl;
			exit(1);
		}

		if (lineIndex == sIndex) {	// For the current server, get its bind ip:port
			string myBind;
			size_t found = line.find(',');
			if (found != string::npos) {
				myBind = line.substr(line.find(',') + 1);
			} else {
				myBind = forward;	// bind = forward if only one address given
			}
			myBindIP = myBind.substr(0, myBind.find(':'));				// Get bind IP
			found = myBind.find(':');
			if (found == string::npos) {
				cerr << "Error: Invalid bind address at index " << lineIndex << endl;
				exit(1);
			}

			try {														// Get bind port
				myBindPort = stoi(myBind.substr(myBind.find(':') + 1), NULL, 10);
			}
			catch (...) {
				cerr << "Error: Invalid bind port at index " << lineIndex << endl;
				exit(1);
			}
			if (myBindPort < 1 || myBindPort > 65535) {
				cerr << "Error: Invalid bind port at index " << lineIndex << endl;
				exit(1);
			}

		}

	}

	if (lineIndex < sIndex) {
		cerr << "Error: Invalid server index (" << lineIndex << " servers listed in the file)\n";
		exit(1);
	}


	signal(SIGINT, intHandler);   // Set a handler for SIGING signals (Crtl + C)

	// bind to the socket for client
	sock = socket(PF_INET, SOCK_DGRAM, 0);

	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(myBindPort);
	inet_pton(AF_INET, myBindIP.c_str(), &(servaddr.sin_addr));

	if ( ::bind(sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) ) {
		cerr << "Error on binding" << endl;
		exit(1);
	}

	// Fill in forwardAddrs
	for (int i = 1; i <= forwardIPs.size(); i++) {
		bzero(&forwardAddrs[i], sizeof(forwardAddrs[i]));
		forwardAddrs[i].sin_family = AF_INET;
		forwardAddrs[i].sin_port = htons(forwardPorts[i - 1]);
		inet_pton(AF_INET, forwardIPs[i - 1].c_str(), &(forwardAddrs[i].sin_addr));
	}




	// For FIFO
	// S[C][g] = client C's counter for room g
	unordered_map<string, array<int, MAX_ROOM>> S;
	// R[C][g] = highest counter value this server has seen from client C in room g
	unordered_map<string, array<int, MAX_ROOM>> R;
	// holdback queues, clientSender (ip:port) => room# => msgID => msg string
	unordered_map<string, array<map<int, string>, MAX_ROOM>> holdback;



	// For TOTAL
	// P[g] = Highest sequence# this server has proposed to group g
	int P[MAX_ROOM] = {};
	// A[g] = Highest 'agreed' sequence# this server has seen for group g
	int A[MAX_ROOM] = {};
	// holdback queues, propose # => sender serverID => deliverable? & msg string
	map<int, map<int, pair<bool, string>> > totalHold[MAX_ROOM];
	// pMax[g][msg]: current maximum of proposed # for msg received for a room
	unordered_map<string, int> pMax[MAX_ROOM];
	// pCount[g][msg]: count for proposed # for msg received for a room
	unordered_map<string, int> pCount[MAX_ROOM];





	while (true) {
		// Receive
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		char buf[BUF_SIZE];
		int rlen = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&src, &srclen);
		buf[rlen] = 0;

		// Get the client's ip:port
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(src.sin_addr), ip, INET_ADDRSTRLEN);

		string recvIP(ip);
		int recvPort = ntohs(src.sin_port);
		string clientID = string(recvIP);
		clientID += ':';
		clientID += to_string(recvPort);

		string line(buf);

		if (line.substr(0, 5) == "/join" && line.size() > 6) {

			int roomNum = -1;
			try { // Check for valid room#
				roomNum = stoi(line.substr(6), NULL, 10);
			}
			catch (...) {
				char msg[] = "-ERR Invalid room number";
				sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));
				continue;
			}
			if (roomNum < 1 || roomNum > MAX_ROOM) {
				char msg[50];
				sprintf(msg, "-ERR Range of room # is 1-%d", MAX_ROOM);
				sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));
				continue;
			}

			// Check if the client is in a room
			auto it = roomMap.find(clientID);
			if (it == roomMap.end()) { // If not in a room
				roomMap[clientID] = roomNum;
				rooms[roomNum - 1][clientID] = src;

				char msg[50];
				sprintf(msg, "+OK You are now in chat room #%d", roomNum);
				sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));

				if (debugMode) cout << getTime() << " S"  << sIndex << " <" << clientID << "> joins room #" << roomNum << endl;
			}
			else {                     // If already in a room
				char msg[50];
				sprintf(msg, "-ERR You are already in room #%d", it->second);
				sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));
			}

		}

		else if (line == "/part") {
			auto it = roomMap.find(clientID);
			if (it == roomMap.end()) { // If not in a room
				char msg[] = "-ERR You are not in a chat room";
				sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));
			} else {                   // If in a room
				int num = it->second;
				rooms[num - 1].erase(clientID); // Remove client from the room
				roomMap.erase(it);

				char msg[50];
				sprintf(msg, "+OK You have left chat room #%d", num);
				sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));
				if (debugMode) cout << getTime() << " S"  << sIndex << " <" << clientID << "> leaves room #" << num << endl;
			}

		}

		else if (line.substr(0, 5) == "/nick" && line.size() > 6) {

			nickMap[clientID] = line.substr(6);

			char msg[50];
			sprintf(msg, "+OK Nickname set to '%s'", line.substr(6).c_str());
			sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));
		}

		else if (line[0] == '/') {
			char msg[] = "-ERR Invalid command";
			sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));
		}
        
        
        
		else {

			bool fromServer = false;
			int sender = 0;				// sender server index
			// check if it's a multicast from a server
			for (int i = 0; i < forwardIPs.size(); i++) {
				if (forwardIPs[i] == recvIP && forwardPorts[i] == recvPort) {
					fromServer = true;
					sender = i + 1;
					break;
				}
			}

			if (fromServer) {	// from a server

				if (order == UNORDERED) { // format: "[roomNum],[message]"
					int roomNum = 0;
					try {	// Get chat room number
						roomNum = stoi(line.substr(0, line.find(',')), NULL, 10);
					}
					catch (...) {
						cerr << "Error: Invalid room # in multicast" << endl;
						exit(1);
					}
					if (roomNum < 1 || roomNum > MAX_ROOM) {
						cerr << "Error: Invalid room # in multicast" << endl;
						exit(1);
					}
					string msgStr = line.substr(line.find(',') + 1);
					if (msgStr.size() != 0) {
						deliver(roomNum, msgStr.c_str());	//send to conneted clients in the room
					}



				} else if (order == FIFO) { // format: "[msgID],[roomNum],[clientSender],[message]"
					// TODO change to "[msgID],[roomNum],[clientSender],[message]"
					// Get msgID and roomNum
					int msgID = 0;
					int roomNum = 0;
					string rest1 = line.substr(line.find(',') + 1);		// substr: [roomNum],[clientSender],[message]
					string rest2 = rest1.substr(rest1.find(',') + 1);	// substr: [clientSender],[message]
					string clientSender = rest2.substr(0, rest2.find(','));
					try {	// Get chat room number
						msgID = stoi(line.substr(0, line.find(',')), NULL, 10);
						roomNum = stoi(rest1.substr(0, rest1.find(',')), NULL, 10);
					}
					catch (...) {
						cerr << "Error in fifo multicast" << endl;
						cerr << line << endl;
						exit(1);
					}
					if (msgID <= 0 || roomNum < 1 || roomNum > MAX_ROOM || clientSender.size() == 0) {
						cerr << "Error in fifo multicast" << endl;
						cerr << line << endl;
						exit(1);
					}
					string msgStr = rest2.substr(rest2.find(',') + 1); // <clientSender> xxxx

					// R[C][g]: highest counter value this server has seen from client C in room g
					// holdback queues, clientSender (ip:port) => room# => msgID => msg string
					holdback[clientSender][roomNum - 1][msgID] = msgStr;

					// initialize int[] R[clientSender]
					auto rIt = R.find(clientSender);
					if (rIt == R.end()) R[clientSender] = {};

					int nextID = R[clientSender][roomNum - 1] + 1;
					auto it = holdback[clientSender][roomNum - 1].find(nextID);
					while (it != holdback[clientSender][roomNum - 1].end()) {
						string toSend = it->second;
						deliver(roomNum, toSend.c_str());
						holdback[clientSender][roomNum - 1].erase(nextID);

						nextID = (++R[clientSender][roomNum - 1]) + 1;
						it = holdback[clientSender][roomNum - 1].find(nextID);
					}







				} else { 	 	// Total ordering
					// message:  [room#],[message]
					// propose:  ?[room#],[propose#],[message]
					// decision: ![room#],[max propose#],[message]

					if (line[0] == '?') {		 // a propose "?[roomNum],[propose#],[msg]"

						int roomNum = 0;
						int propose = 0;
						string rest = line.substr(line.find(',') + 1);		// substr: [propose#],[msg]
						try {	// Get chat room number
							roomNum = stoi(line.substr(1, line.find(',')), NULL, 10);
							propose = stoi(rest.substr(0, rest.find(',')), NULL, 10);
						}
						catch (...) {
							cerr << "Error in total multicast propose msg" << endl;
							exit(1);
						}
						if (roomNum < 1 || roomNum > MAX_ROOM || propose <= 0) {
							cerr << "Error in total multicast propose msg" << endl;
							exit(1);
						}
						string msgStr = rest.substr(rest.find(',') + 1);

						// initialize pMax & pCount
					    auto maxIt = pMax[roomNum -1].find(msgStr);
						if (maxIt == pMax[roomNum -1].end()) pMax[roomNum -1][msgStr] = 0;
						auto countIt = pCount[roomNum -1].find(msgStr);
						if (countIt == pCount[roomNum -1].end()) pCount[roomNum -1][msgStr] = 0;

						pMax[roomNum - 1][msgStr] = max(pMax[roomNum - 1][msgStr], propose);
						pCount[roomNum - 1][msgStr]++;
						if (pCount[roomNum - 1][msgStr] == forwardIPs.size()) { // all proposes received, send a decision

							string decMsg = "!";	// decision message
							decMsg += to_string(roomNum);
							decMsg += ',';
							decMsg += to_string(pMax[roomNum - 1][msgStr]);
							decMsg += ',';
							decMsg += msgStr;

							pMax[roomNum -1].erase(msgStr);
							pCount[roomNum -1].erase(msgStr);

							multicast(decMsg.c_str());
							sendto(sock, decMsg.c_str(), decMsg.size(), 0, (struct sockaddr*) & (forwardAddrs[sIndex]), sizeof(forwardAddrs[sIndex]));
						}



					} else if (line[0] == '!') { // a decision on propose#: "![room#],[max propose#],[message]"

						int roomNum = 0;
						int propose = 0;
						string rest = line.substr(line.find(',') + 1);		// substr: [propose#],[msg]
						try {	// Get chat room number
							roomNum = stoi(line.substr(1, line.find(',')), NULL, 10);
							propose = stoi(rest.substr(0, rest.find(',')), NULL, 10);
						}
						catch (...) {
							cerr << "Error in total multicast propose msg" << endl;
							exit(1);
						}
						if (roomNum < 1 || roomNum > MAX_ROOM || propose <= 0) {
							cerr << "Error in total multicast propose msg" << endl;
							exit(1);
						}
						string msgStr = rest.substr(rest.find(',') + 1);

						// TOTAL: holdback queues, room# => propose # => sender serverID => deliverable? & msg string
						// map<int, map< int, pair<bool,string> >> totalHold[MAX_ROOM]

						// Get the propose# before decision
						int oldP = 0;		
						auto it = totalHold[roomNum - 1].begin();
						while (it != totalHold[roomNum -1].end()){
							auto itInside = it->second.find(sender);
							if (itInside != it->second.end() && itInside->second.second == msgStr){
								oldP = it->first;
								break;
							}
						}
						if (oldP <= 0){
							cerr << "Error in total multicast finding oldP" << endl;
							exit(1);
						}

						if (propose != oldP) { // reorder the holdback if prop# changes
							totalHold[roomNum - 1][propose][sender] = totalHold[roomNum - 1][oldP][sender];
							totalHold[roomNum - 1][oldP].erase(sender);
							// erase an outer entry if its inner map is empty
							if (totalHold[roomNum - 1][oldP].empty())
								totalHold[roomNum - 1].erase(oldP);
						}
						totalHold[roomNum - 1][propose][sender].first = true; // set as deliverable
						A[roomNum - 1] = max(A[roomNum - 1], propose);


						it = totalHold[roomNum - 1].begin();
						while (it != totalHold[roomNum - 1].end()) {
							auto itInside = it->second.begin();
							while (itInside != it->second.end()) {
								if (itInside->second.first == false) {
									goto endloop;
								}
								string targetMsg = itInside->second.second;
								deliver(roomNum, targetMsg.c_str());
								itInside = it->second.erase(itInside);	// erase the sent inner entry
							}
							it = totalHold[roomNum - 1].erase(it); // erase the empty outer entry
						}
						endloop: ;




					} else {					 // a message in TOTAL, "[roomNum],[message]"

						int roomNum = 0;
						try {	// Get chat room number
							roomNum = stoi(line.substr(0, line.find(',')), NULL, 10);
						}
						catch (...) {
							cerr << "Error: Invalid room # in multicast" << endl;
							exit(1);
						}
						if (roomNum < 1 || roomNum > MAX_ROOM) {
							cerr << "Error: Invalid room # in multicast" << endl;
							exit(1);
						}
						// send back propose "?[room#],[propose#],[msg]"
						P[roomNum - 1] = max(P[roomNum - 1], A[roomNum - 1]) + 1;
						string propMsg = "?";
						propMsg += line.substr(0, line.find(',') + 1); //[room#],
						propMsg += to_string(P[roomNum - 1]);
						propMsg += ',';

						string msgStr = line.substr(line.find(',') + 1);
						propMsg += msgStr;
						sendto(sock, propMsg.c_str(), propMsg.size(), 0, (struct sockaddr*) & (forwardAddrs[sender]), sizeof(forwardAddrs[sender]));

						// put the msg into holdback and mark as undeliverable
						totalHold[roomNum - 1][ P[roomNum - 1] ][sender] = pair<bool, string> (false, msgStr);

					}
				}



			} else { 		// Msg from client

				if (line.size() == 0) continue;

				// Check if the client is in a room
				auto it = roomMap.find(clientID);
				if (it == roomMap.end()) { // If not in a room
					char msg[] = "-ERR You are not in a chat room";
					sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&src, sizeof(src));
				} else {
					// Add <username> to the message
					string msgStr ("<");
					auto nickIter = nickMap.find(clientID);
					if (nickIter == nickMap.end()) {
						msgStr += clientID;
					} else {
						msgStr += nickIter->second;
					}
					msgStr += "> ";
					msgStr += line;


					int roomNum = it->second;

					string servMsg = to_string(roomNum);
					servMsg += ',';
					servMsg += msgStr;

					if (order == UNORDERED) {
						deliver(roomNum, msgStr.c_str());		// send to conneted clients in the room
						multicast(servMsg.c_str());				// send to other servers

					} else if (order == FIFO) {					// multicast format: "[msgID],[roomNum],[clientID],[message]"
						deliver(roomNum, msgStr.c_str());

						// S[C][g] = client C's counter for room g
						// Initalize int[] S[clientID]
						auto sIt = S.find(clientID);
						if (sIt == S.end()) S[clientID] = {};

						int msgID = ++S[clientID][roomNum - 1];
						string fifoMsg(to_string(msgID));
						fifoMsg += ',';
						fifoMsg += to_string(roomNum);
						fifoMsg += ',';
						fifoMsg += clientID;
						fifoMsg += ',';
						fifoMsg += msgStr;
						multicast(fifoMsg.c_str());

					} else {	// total order
						multicast(servMsg.c_str());
						// send to itself
						sendto(sock, servMsg.c_str(), servMsg.size(), 0, (struct sockaddr*) & (forwardAddrs[sIndex]), sizeof(forwardAddrs[sIndex]));

					}
					if (debugMode) cout << getTime() << " S"  << sIndex << " <" << clientID << "> posts \"" << line << "\" to room #" << roomNum << endl;
				}

			}

		}

	}

	return 0;
}
