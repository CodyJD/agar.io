//
//Server for multi-player game
//author: Gordon Griesel
//date: Winter 2018
//
//Handle multiple socket connections with select and fd_set on Linux.
//This does not need forks or threads.
//resources:
//http://www.binarytides.com/multiple-socket-connections-fdset-select-linux/
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#define TRUE 1
#define FALSE 0
#define PORT 4490
const int MAX_CLIENTS = 60;

class Player {
public:
	int pos[2];
	int number;
	char name[64];
	int following;
} player[MAX_CLIENTS];

class Global {
public:
	int port;
	int master_socket;
	int max_sd;
	//Set of socket descriptors.
	fd_set readfds;
	struct sockaddr_in address;
	int addrlen;
	int client_socket[MAX_CLIENTS];
	int nclients;
	int nplayers;
	char *grid;
	int h, w;
	int playerMoved;
	int total_moves[MAX_CLIENTS];
	int longestMessage;
	Global() {
		memset(client_socket, 0, sizeof(int)*MAX_CLIENTS);
		nclients = 0;
		nplayers = 0;
		grid = NULL;
		playerMoved = -1;
		memset(total_moves, 0, sizeof(int)*MAX_CLIENTS);
		longestMessage = 0;
	}
} g;

//-----------------------------------------------------------------------------
//Setup timers
//clock_gettime(CLOCK_REALTIME, &timeStart);
const double oobillion = 1.0 / 1e9;
struct timespec timeStart, timeEnd;
//double physicsCountdown=0.0, timeSpan=0.0, timeFPS=0.0;
double timeDiff(struct timespec *start, struct timespec *end) {
	return (double)(end->tv_sec - start->tv_sec ) +
			(double)(end->tv_nsec - start->tv_nsec) * oobillion;
}
void timeCopy(struct timespec *dest, struct timespec *source) {
	memcpy(dest, source, sizeof(struct timespec));
}
//-----------------------------------------------------------------------------

void setupServer();
void messageFromClient(int playerIdx, char *buffer);
void newPlayerIsConnecting();
void playerIsDisconecting(int idx);

//==========================================================================
//main
//==========================================================================
int main(int argc , char *argv[])
{
	printf("Usage:                       \n");
	printf("%s <port> <width> <height>   \n", argv[0]);
	printf("All arguments are optional.  \n");
	g.port = PORT;
	if (argc > 1)
		g.port = atoi(argv[1]);
	//-----------------------------------	
	//setup the playing field dimensions
	g.w = 60;
	g.h = 6;
	//width
	if (argc > 2)
		g.w = atoi(argv[2]);
	if (argc > 3)
		g.h = atoi(argv[3]);
	g.grid = new char[g.h * g.w];
	//-----------------------------------	
	int sd;
	char buffer[1640];
	//char message[400] = "";
	setupServer();
	clock_gettime(CLOCK_REALTIME, &timeStart);
	printf("Waiting for connections ...\n");
	while (TRUE) {
		//Clear the socket set, add master, add children.
		FD_ZERO(&g.readfds);
		FD_SET(g.master_socket, &g.readfds);
		for (int i=0; i<g.nclients; i++) {
			FD_SET(g.client_socket[i], &g.readfds);
		}
		//Wait for an activity on one of the sockets.
		//---------------------------------------------
		//SELECT IS HERE
		//---------------------------------------------
		int activity = select(g.max_sd + 1, &g.readfds, NULL, NULL, NULL);
		if ((activity < 0) && (errno != EINTR)) {
			printf("select error");
		}
		//If something happened on the master socket,
		// then it's an incoming connection
		if (FD_ISSET(g.master_socket, &g.readfds)) {
			newPlayerIsConnecting();
		}
		//Some IO operation on a client socket?
		for (int i=0; i<g.nclients; i++) {
			sd = g.client_socket[i];
			if (FD_ISSET(sd, &g.readfds)) {
				//Check if it was for closing,
				// and also read the incoming message
				int mlen = read(sd, buffer, 1024);
				buffer[mlen] = '\0';
				//printf("incoming message: **%s**\n", buffer);
				if (g.longestMessage < mlen)
					g.longestMessage = mlen;
				if (mlen == 0) {
					playerIsDisconecting(i);
				} else {
					messageFromClient(i, buffer);
				}
			}
		}
		//Try to eliminate flicker by slowing down refresh.
		clock_gettime(CLOCK_REALTIME, &timeEnd);
		double diff = timeDiff(&timeStart, &timeEnd);
		if (diff > 0.1) {
			timeCopy(&timeStart, &timeEnd);
			//display the playing grid...
			memset(g.grid, '.', g.h*g.w);
			printf("\nplayers connected: %i\n", g.nplayers);
			printf("moves: ");
			for (int i=0; i<g.nclients; i++) {
				printf("%i ", g.total_moves[i]);
			}
			printf("\n");
			printf("longestMessage %i\n", g.longestMessage);
			int x, y;
			for (int i=0; i<g.nclients; i++) {
				if (g.client_socket[i] > 0) {
					y = (g.h-1)-player[i].pos[1];
					x = player[i].pos[0];
					g.grid[y*g.w+x] = 'A'+i;
				}
			}
			for (int i=0; i<g.h; i++) {
				for (int j=0; j<g.w; j++)
					printf("%c", g.grid[i*g.w+j]);
				printf("\n");
			}
		}
	}
	delete [] g.grid;
	return 0;
}

void messageFromClient(int playerIdx, char *buffer)
{
	//printf("messageFromClient(%i, **%s**)...\n", playerIdx, buffer);
	//Check for multiple messages in the buffer string.
	int k = playerIdx;
	char ts[1640];
	int done = 0;
	char *buff = buffer;
	char *p;
	//any broadcast messages?
	while (!done) {
		done = 1;
		p = strstr(buff, "broadcast:");
		if (p) {
			//printf("broadcast came in.\n");
			//broadcast:hello everyonex#zbroadcast:byex#z");
			//message will end with x#z
			sprintf(ts, "broadcast from: %c: ", (char)'A'+k);	
			char *x = strstr(p+10, "x#z");
			*(x+3) = '\0';
			strcat(ts, p+10);	
			//Echo back the message that came in, to all players.
			int slen = strlen(ts);
			//printf("sending **%s**\n", ts);
			for (int j=0; j<g.nclients; j++) {
				if (g.client_socket[j] > 0)
					send(g.client_socket[j], ts, slen, 0);
			}
			buff = x + 3;
			done = 0;
		}
	}
	done = 0;
	buff = buffer;
	//any move messages?
	//message might be "move:01 02 03move:11 05 22move:02 03 04"
	while (!done) {
		done = 1;
		p = strstr(buff, "move:");
		//move:down
		g.playerMoved = -1;
		if (p) {
			//printf("found move at %s\n", p);
			++g.total_moves[k];
			p += 5;
			if (strncmp(p, "up", 2) == 0) {
				if (++player[k].pos[1] >= g.h)
					player[k].pos[1] = g.h-1;
				g.playerMoved = k;
				buff += 7;
			}
			else if (strncmp(p, "down", 4) == 0) {
				if (--player[k].pos[1] < 0)
					player[k].pos[1] = 0;
				g.playerMoved = k;
				buff += 9;
			}
			else if (strncmp(p, "right", 5) == 0) {
				if (++player[k].pos[0] >= g.w)
					player[k].pos[0] = g.w-1;
				g.playerMoved = k;
				buff += 10;
			}
			else if (strncmp(p, "left", 4) == 0) {
				if (--player[k].pos[0] < 0)
					player[k].pos[0] = 0;
				g.playerMoved = k;
				buff += 9;
			}
			done = 0;
			if (g.playerMoved >= 0) {
				char message[400] = "";
				int pl = g.playerMoved;
				sprintf(message, "move:%2i %2i %2i",
					pl, player[pl].pos[0], player[pl].pos[1]);
				int slen = strlen(message);
				//printf("---sending this: **%s**\n", message); 
                fflush(stdout);
				//send(new_socket, message, slen, 0);
				for (int i=0; i<g.nclients; i++) {
					int sd = g.client_socket[i];
					if (sd > 0) {
						//printf("--sending-- **%s**\n", message);
						send(sd, message, slen, 0);
					}
				}
			}
		}
	}
}

void setupServer()
{
	//Create a master socket.
	if ((g.master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
		perror("socket failed");
		exit(EXIT_FAILURE);
	}
	//Setup master socket to allow multiple connections.
	int opt = TRUE;
	if (setsockopt(g.master_socket, SOL_SOCKET, SO_REUSEADDR,
			(char *)&opt, sizeof(opt)) < 0) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
	//Type of socket created...
	g.address.sin_family = AF_INET;
	g.address.sin_addr.s_addr = INADDR_ANY;
	g.address.sin_port = htons(g.port);
	//Bind the socket to localhost port
	if (bind(g.master_socket,
			(struct sockaddr *)&g.address, sizeof(g.address)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	printf("Listening on port %d \n", g.port);
	//Specify maximum of 30 pending connections for the master socket.
	if (listen(g.master_socket, 30) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}
	//Accept the incoming connection.
	g.addrlen = sizeof(g.address);
	g.max_sd = g.master_socket;
}

void getMaxSocketDescriptor()
{
	g.max_sd = g.master_socket;
	for (int i=0; i<g.nclients; i++) {
		if (g.client_socket[i] > g.max_sd)
			g.max_sd = g.client_socket[i];
	}
}

void broadcastNplayers()
{
	//count how many players...
	g.nplayers = 0;
	for (int i=0; i<g.nclients; i++) {
		if (g.client_socket[i] > 0)
			++g.nplayers;
	}
	char message[400] = "";
	sprintf(message, "nplayers:%2i", g.nplayers);
	int slen = strlen(message);
	for (int i=0; i<g.nclients; i++) {
		if (g.client_socket[i] > 0)
			send(g.client_socket[i], message, slen, 0);
	}
}

void newPlayerIsConnecting()
{
	int new_socket;
	char message[400] = "";
	int slen;
	new_socket = accept(g.master_socket, (struct sockaddr *)&g.address,
		(socklen_t*)&g.addrlen);
	if (new_socket < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}
	//printf("New connection, socket fd: %d, ip: %s, port: %d\n",
	//	new_socket, inet_ntoa(address.sin_addr),
	//	ntohs(address.sin_port));
	//Add new socket to array of socket handles.
	//Look for first vacant socket spot.
	int vacant_spot;
	for (int i=0; i<=g.nclients; i++) {
		if (g.client_socket[i] == 0) {
			vacant_spot = i;
			g.client_socket[vacant_spot] = new_socket;
			break;
		}
	}
	//------------------------------------------
	//send the player the player-number.
	sprintf(message, "yournumber:%i", vacant_spot);
	slen = strlen(message);
	//printf("---sending this: **%s**\n", message); fflush(stdout);
	send(new_socket, message, slen, 0);
	//
	sprintf(message, "gridsize:%2i %2i", g.w, g.h);
	slen = strlen(message);
	//printf("---sending this: **%s**\n", message); fflush(stdout);
	send(new_socket, message, slen, 0);
	//-----------------------------------------------------------
	//tell new player the position of all other players.
	for (int i=0; i<g.nclients; i++) {
		int sd = g.client_socket[i];
		if (sd > 0) {
			sprintf(message, "newplayer:%2i %2i %2i",
				i, player[i].pos[0], player[i].pos[1]);
			slen = strlen(message);
			send(new_socket, message, slen, 0);
			//printf("1 **%s** sent <---\n", message); fflush(stdout);
		}
	}
	//------------------------------------------
	//player[vacant_spot].pos[0] = g.nclients;
	//------------------------------------------
	printf("Adding to list of sockets as %i at index %i\n",
		new_socket, vacant_spot);
	if (vacant_spot == g.nclients)
		++g.nclients;
	//-----------------------------------------------------------
	//broadcast that new player joined game
	//-----------------------------------------------------------
	int pl = vacant_spot;
	player[pl].pos[0] = pl;
	player[pl].pos[1] = 0;
	sprintf(message, "newplayer:%2i %2i %2i",
		pl, player[pl].pos[0], player[pl].pos[1]);
	slen = strlen(message);
	//printf("sending %i newplayer messages\n", g.nclients); fflush(stdout);
	for (int i=0; i<g.nclients; i++) {
		if (g.client_socket[i] > 0) {
			send(g.client_socket[i], message, slen, 0);
			printf("2 **%s** sent <--- sd: %i\n", message, g.client_socket[i]);
			fflush(stdout);
		}
	}
	broadcastNplayers();
	getMaxSocketDescriptor();
}

void playerIsDisconecting(int idx)
{
	int sd = g.client_socket[idx];
	getpeername(sd, (struct sockaddr*)&g.address, (socklen_t*)&g.addrlen);
	printf("Host disconnected, ip: %s, port: %d\n",
		inet_ntoa(g.address.sin_addr),
		ntohs(g.address.sin_port));
	//Close the socket and mark as 0 in list, for reuse
	close(sd);
	g.client_socket[idx] = 0;
	if (idx == g.nclients-1)
		--g.nclients;
	//
	char message[400] = "";
	int slen;
	sprintf(message, "playergone:%2i", idx);
	slen = strlen(message);
	//printf("sending %i newplayer messages\n", g.nclients); fflush(stdout);
	for (int i=0; i<g.nclients; i++) {
		if (g.client_socket[i] > 0) {
			send(g.client_socket[i], message, slen, 0);
		}
	}
	broadcastNplayers();
	getMaxSocketDescriptor();
}











