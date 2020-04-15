//Modified by: Cody Davis
//Description: it does shit.
//
//
//
//Client for multi-player game
//author: Gordon Griesel
//date: Winter 2018
//
//client program
//communicates with a server
//resources:
//http://www.linuxhowtos.org/C_C++/socket.htm

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <pthread.h>
const int MAX_CLIENTS = 30;
const int MAX_MESSAGES = 4;
void removeCRLF(char *str);
void error(const char *msg);

struct Message {
	char str[1640];
	Message *next;
	Message() {
		//constructor is important
		*str = '\0';
		next = NULL;
	}
};

class Player {
public:
	int pos[2];
	int number;
	char name[60];
};

class Global {
public:
	struct sockaddr_in serv_addr;
	struct hostent *server;
	int sockfd;
	pthread_t thread;
	Message *mhead;
	Player players[MAX_CLIENTS];
	int nplayers;
	int high_playernum;
	int player_number;
	char player_name[60];
	char messQueue[MAX_MESSAGES][1640];
	int nQueue;
	char *grid;
	int w, h;
	unsigned int sflag, eflag;
	Global() {
		mhead = NULL;
		grid = NULL;
		for (int i=0; i<MAX_CLIENTS; i++)
			players[i].number = -1;
		strcpy(player_name, "test");
		high_playernum = 0;
		nQueue=0;
		for (int i=0; i<MAX_MESSAGES; i++)
			*messQueue[i] = '\0';
		sflag = eflag = 0;
	}
	~Global() {
		if (grid != NULL)
			delete [] grid;
	}
} g;

//-----------------------------------------------------------------------------
//Setup timers
//clock_gettime(CLOCK_REALTIME, &timeStart);
const double oobillion = 1.0 / 1e9;
struct timespec timeStart, timeEnd, timeCurrent;
double physicsCountdown=0.0, timeSpan=0.0, timeFPS=0.0;
double timeDiff(struct timespec *start, struct timespec *end) {
	return (double)(end->tv_sec - start->tv_sec ) +
			(double)(end->tv_nsec - start->tv_nsec) * oobillion;
}
void timeCopy(struct timespec *dest, struct timespec *source) {
	memcpy(dest, source, sizeof(struct timespec));
}
//-----------------------------------------------------------------------------


int connectWithServer(char *host, int port)
{
	printf("connectWithServer(%s, %i)...\n", host, port);
	g.sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (g.sockfd < 0) { 
		error("ERROR opening socket");
		return 1;
	}
	g.server = gethostbyname(host);
	if (g.server == NULL) {
		fprintf(stderr, "ERROR, no such host\n");
		return 1;
	}
	memset(&g.serv_addr, 0, sizeof(g.serv_addr));
	g.serv_addr.sin_family = AF_INET;
	memmove(&g.serv_addr.sin_addr.s_addr, g.server->h_addr,
		g.server->h_length);
	g.serv_addr.sin_port = htons(port);
	int ret =
		connect(g.sockfd, (struct sockaddr *)&g.serv_addr,
		sizeof(g.serv_addr)); 
	if (ret < 0) {
		error("ERROR connecting");
		return 1;
	}
	return 0;
}

int writeToSocket(char *mess)
{
	int n = write(g.sockfd, mess, strlen(mess));
	if (n < 0) 
		error("ERROR writing to socket");
	return n;
}

void closeConnection()
{
	close(g.sockfd);
}

int kbhit() {
	//http://cboard.cprogramming.com/c-programming/63166-kbhit-linux.html
	struct termios oldt, newt;
	int ch, oldf;
	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
	ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	fcntl(STDIN_FILENO, F_SETFL, oldf);
	if (ch != EOF) {
		ungetc(ch, stdin);
		return 1;
	}
	return 0;
}

//http://www.alexonlinux.com/multithreaded-simple-data-type-access-and-atomic-variables

void *readFromSocket(void *arg)
{
	printf("readFromSocket() thread starting...\n"); fflush(stdout);
	int nempty = 0;
	char ts[1640];
	int n;
	while (1) {
		memset(ts, 0, 1640);
		n = read(g.sockfd, ts, 1600);
		//printf("===> n read: %i\n", n); fflush(stdout);
		if (n < 0) {
			printf("ERROR reading from socket.\n");
			goto threaderror;
		}
		ts[n] = '\0';
		if (n == 0) {
			if (++nempty > 10) {
				goto threaderror;
			}
		} else {
			nempty = 0;
			//add this message to linked-list
			//stay here until startflag and endflag are equal
			while (__sync_fetch_and_add(&g.sflag, 0) !=
				__sync_fetch_and_add(&g.eflag, 0)) {}
			Message *m = new Message;
			strcpy(m->str, ts);
			printf("linked list message ===> **%s**\n", m->str); fflush(stdout);
			m->next = g.mhead;
			g.mhead = m;
			__sync_fetch_and_add(&g.sflag, 1);
		}
	}
threaderror:;
	printf("Server is not communicating.\n");
	printf("This program is shutting down now.\n");
	exit(0);
	//
	return (void *)0;
}

//============================================================================
//main
//============================================================================
int main(int argc, char *argv[])
{
	srand((unsigned)time(NULL));
	if (argc < 2) {
		printf("Usage: %s <hostname> <port> <name>        \n", argv[0]);
		printf("examples: %s odin 51717 TheReaper         \n", argv[0]);
		printf("          %s odin.cs.csub.edu 8888 PacMan \n", argv[0]);
		printf("          %s test                         \n", argv[0]);
		fflush(stdout);
		exit(0);
	}
	//---------------------------------------------
	//connect with server, create thread
	//---------------------------------------------
	char sname[256];
	int portno;
	strcpy(g.player_name, "noname");
	if (argc == 2) {
		strcpy(sname, "localhost");
		portno = 4490;
	} else if (argc == 3) {
		strcpy(sname, argv[1]);
		portno = atoi(argv[2]);
	} else if (argc == 4) {
		strcpy(sname, argv[1]);
		portno = atoi(argv[2]);
		strcpy(g.player_name, argv[3]);
	}
	int ret = connectWithServer(sname, portno);
	if (ret == 0) {
		usleep(100000);
		void *v = (void*)"";
		int xret = pthread_create(&g.thread, NULL, readFromSocket, v);
		if (xret) {
			fprintf(stderr, "Error pthread_create() xret: %i\n", xret);
			exit(EXIT_FAILURE);
		}
	} else {
		printf("No connection with server. Try again later.\n");
		exit(0);
	}
	//-----------------------------------------------------------------------
	//get your initial information from server
	char mess[1640];
	printf("check server message...\n");
	//-----------------------------------------------------------------------
	void autoMove();
	char ts[1640];
	char inp = '\0';
	while (1) {
		inp = '\0';
		if (kbhit()) {
			inp = getchar();
			switch (inp) {
				case 27:
					//the first value is esc
					//then [
					inp = getchar();
					inp = getchar();
					switch(inp) {
						case 'A': writeToSocket((char *)"move:up"); break;
						case 'B': writeToSocket((char *)"move:down"); break;
						case 'C': writeToSocket((char *)"move:right"); break;
						case 'D': writeToSocket((char *)"move:left"); break;
					}
					break;
				case '1':                    
					//broadcast a message to all
					memset(mess, 0, 256);
					printf("Enter a message: ");
					fgets(mess, 255, stdin);
					removeCRLF(mess);
					if (strlen(mess) <= 0)
						break;
					removeCRLF(mess);
					sprintf(ts, "broadcast:%sx#z", mess);
					printf("sending server this message **%s**\n", ts);
					writeToSocket(ts);
					break;
				case 'M':
					autoMove();
					break;
				case 'q':
					closeConnection();
					return 0;
			}
		}
		//Get next message from server.
		int refresh=0;
		if (g.mhead) {
			char *s, *p;
			Message *m = g.mhead;
			int slen = strlen(m->str);
			if (slen > 0) {
				s = m->str;
				char *end = s + slen;
				do {
					p = strstr(s, "move:");
					if (p) {
						//move:12 34 56
						//012345678901234567890
						int pl = atoi(p +  5);
						int x  = atoi(p +  8);
						int y  = atoi(p + 11);
						g.players[pl].pos[0] = x;
						g.players[pl].pos[1] = y;
						refresh = 1;
						s = p+13;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "newplayer:");
					if (p) {
						//newplayer:02 34  4
						//012345678901234567890
						int pl = atoi(p + 10);
						int x  = atoi(p + 13);
						int y  = atoi(p + 16);
						printf("received newplayer <---- %i %i %i\n",pl, x, y);
						g.players[pl].pos[0] = x;
						g.players[pl].pos[1] = y;
						g.players[pl].number = pl;
						refresh = 1;
						s = p+18;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "playergone:");
					if (p) {
						printf("a player left <----\n");
						//playergone: 2
						//012345678901234567890
						int pl = atoi(p + 11);
						g.players[pl].number = -1;
						s = p+13;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "nplayers:");
					if (p) {
						//nplayers: 02
						//012345678901234567890
						g.nplayers = atoi(p + 9);
						refresh = 1;
						s = p+12;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "yournumber:");
					if (p) {
						//yournumber:01
						//012345678901234567890
						int a = atoi(p+11);
						printf("You are player number: %i\n", a);
						//g.players[a].pos[0] = a;
						//g.players[a].pos[1] = 0;
						g.players[a].number = a;
						g.player_number = a;
						printf("setup local player...\n");
						strcpy(g.players[g.player_number].name, g.player_name);
						s = p+13;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "gridsize:");
					if (p) {
						//gridsize:64 48
						//012345678901234567890
						g.w = atoi(p+9);
						g.h = atoi(p+12);
						printf("Grid is: %ix%i\n", g.w, g.h);
						if (g.grid != NULL)
							delete [] g.grid;
						g.grid = new char[g.w * g.h];
						refresh = 1;
						s = p+14;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "broadcast from:");
					if (p) {
						//broadcast from:
						//012345678901234567890
						char *x = strstr(p, "x#z");
						if (x) {
							*x = '\0';
							for (int i=MAX_MESSAGES-1; i>0; i--)
								strcpy(g.messQueue[i], g.messQueue[i-1]);
							strcpy(g.messQueue[0], p);
							s = x+3;
							refresh = 1;
						} else {
							s = p+14;
						}
					}
				} while (p && s < end);
			}
			printf("freeing.\n"); fflush(stdout);
			//stay here until flags are not equal.
			while (__sync_fetch_and_add(&g.sflag, 0) ==
				__sync_fetch_and_add(&g.eflag, 0)) {}
			g.mhead = m->next;
			delete m;
			__sync_fetch_and_add(&g.eflag, 1);
			//printf("freed.\n"); fflush(stdout);
		}
		//
		if (refresh && g.grid != NULL) {
			//display the playing grid...
			printf("press B to broadcast a message to all players.\n");
			for (int i=MAX_MESSAGES-1; i>=0; i--)
				printf("%s\n", g.messQueue[i]);
			printf("nplayers: %i   you are: %c\n",
				g.nplayers, (char)('A'+g.player_number));
			memset(g.grid, '.', g.h*g.w);
			int x, y;
			//for (int i=0; i<g.nplayers; i++) {
			for (int i=0; i<MAX_CLIENTS; i++) {
				if (g.players[i].number < 0)
					continue;
				y = (g.h-1) - g.players[i].pos[1];
				x = g.players[i].pos[0];
				g.grid[y*g.w+x] = 'A'+g.players[i].number;
			}
			for (int i=0; i<g.h; i++) {
				for (int j=0; j<g.w; j++)
					printf("%c", g.grid[i*g.w+j]);
				printf("\n");
			}
		}
	}
	//-----------------------------------------------------------------------
	closeConnection();
	return 0;
}

void autoMove()
{
	clock_gettime(CLOCK_REALTIME, &timeStart);
	char inp = '\0';
	while (1) {
		inp = '\0';
		if (kbhit()) {
			inp = getchar();
			switch (inp) {
				case 'M':
					return;
				case 'q':
					ungetc('q', stdin);
					return;
			}
		}
		clock_gettime(CLOCK_REALTIME, &timeEnd);
		double diff = timeDiff(&timeStart, &timeEnd);
		if (diff > 0.1) {
			for (int i=0; i<2; i++) {
				int r = rand() & 3;
				switch (r) {
					case 0: writeToSocket((char *)"move:up"); break;
					case 1: writeToSocket((char *)"move:down"); break;
					case 2: writeToSocket((char *)"move:right"); break;
					case 3: writeToSocket((char *)"move:left"); break;
				}
			}
			timeCopy(&timeStart, &timeEnd);
		}
		//Get next message from server.
		int refresh=0;
		if (g.mhead) {
			char *s, *p;
			Message *m = g.mhead;
			int slen = strlen(m->str);
			if (slen > 0) {
				//printf("**%s**\n", m->str);
				s = m->str;
				char *end = s + slen;
				do {
					p = strstr(s, "move:");
					if (p) {
						//move:12 34 56
						//012345678901234567890
						int pl = atoi(p +  5);
						int x  = atoi(p +  8);
						int y  = atoi(p + 11);
						g.players[pl].pos[0] = x;
						g.players[pl].pos[1] = y;
						refresh = 1;
						s = p+13;
					}
				} while (p && s < end);
				s = m->str;
				do {
					//printf("s: **%s**\n", s);
					p = strstr(s, "newplayer:");
					if (p) {
						//newplayer:02 34  4
						//012345678901234567890
						int pl = atoi(p + 10);
						int x  = atoi(p + 13);
						int y  = atoi(p + 16);
						printf("received newplayer <---- %i %i %i\n",pl, x, y);
						g.players[pl].pos[0] = x;
						g.players[pl].pos[1] = y;
						g.players[pl].number = pl;
						refresh = 1;
						s = p+18;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "playergone:");
					if (p) {
						printf("a player left <----\n");
						//playergone: 2
						//012345678901234567890
						int pl = atoi(p + 11);
						g.players[pl].number = -1;
						s = p+13;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "nplayers:");
					if (p) {
						//nplayers: 02
						//012345678901234567890
						g.nplayers = atoi(p + 9);
						refresh = 1;
						s = p+12;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "yournumber:");
					if (p) {
						//yournumber:01
						//012345678901234567890
						int a = atoi(p+11);
						printf("You are player number: %i\n", a);
						g.players[a].number = a;
						g.player_number = a;
						printf("setup local player...\n");
						strcpy(g.players[g.player_number].name, g.player_name);
						s = p+13;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "gridsize:");
					if (p) {
						//gridsize:64 48
						//012345678901234567890
						g.w = atoi(p+9);
						g.h = atoi(p+12);
						printf("Grid is: %ix%i\n", g.w, g.h);
						if (g.grid != NULL)
							delete [] g.grid;
						g.grid = new char[g.w * g.h];
						refresh = 1;
						s = p+14;
					}
				} while (p && s < end);
				s = m->str;
				do {
					p = strstr(s, "broadcast from:");
					if (p) {
						//broadcast from:
						//012345678901234567890
						char *x = strstr(p, "x#z");
						if (x) {
							*x = '\0';
							for (int i=MAX_MESSAGES-1; i>0; i--)
								strcpy(g.messQueue[i], g.messQueue[i-1]);
							strcpy(g.messQueue[0], p);
							s = x+3;
							refresh = 1;
						} else {
							s = p+14;
						}
					}
				} while (p && s < end);
			}
			//printf("freeing.\n"); fflush(stdout);
			//must be 1
			while (__sync_fetch_and_add(&g.sflag, 0) ==
				__sync_fetch_and_add(&g.eflag, 0)) {}
			__sync_fetch_and_add(&g.eflag, 1);
			g.mhead = m->next;
			delete m;
		}
		//
		if (refresh && g.grid != NULL) {
			//display the playing grid...
			printf("press B to broadcast a message to all players.\n");
			for (int i=MAX_MESSAGES-1; i>=0; i--)
				printf("%s\n", g.messQueue[i]);
			printf("nplayers: %i   you are: %c\n",
				g.nplayers, 'A' + g.player_number);
			memset(g.grid, '.', g.h*g.w);
			int x, y;
			//for (int i=0; i<g.nplayers; i++) {
			for (int i=0; i<MAX_CLIENTS; i++) {
				if (g.players[i].number < 0)
					continue;
				y = (g.h-1) - g.players[i].pos[1];
				x = g.players[i].pos[0];
				g.grid[y*g.w+x] = 'A'+g.players[i].number;
			}
			for (int i=0; i<g.h; i++) {
				for (int j=0; j<g.w; j++)
					printf("%c", g.grid[i*g.w+j]);
				printf("\n");
			}
		}
	}
}

void removeCRLF(char *str)
{
	//remove carriage return and newline from end of text.
	char *p = str;
	while (*p) {
		if (*p == 10 || *p == 13) {
			*p = '\0';
			break;
		}
		++p;
	}
}

void error(const char *msg)
{
	perror(msg);
	exit(0);
}

