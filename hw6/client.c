#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "sfwrite.h"
#include <pthread.h>

#define MAX_INPUT 1024

//List of Chats
typedef struct chat
{ 
	char name[50];	//Who are you chatting with?
	int fd; 		//file descriptor of the chat
	int passed_fd; //unused by this program to be closed when finished(part of the socket pair).
	int history_fd;

	int pid;
	int disconnected;
	int chatClosed;
	struct chat* next; 	//next chat in the list
} chat;

//LIST OF COMM THREADS(TO THE CHATS)


//Forward declarations
void parseOpt(int argc, char **argv);
void setCommandFlags(int argc, char **argv);
void initSocket();
void sendConnectionRequest();
void handleLoginProtocol();
void handleNewUserLoginProtocol();
void readInput(char cmd[MAX_INPUT], char* prompt);
void error(char* message);
void catchCtrlC();
void trimNewLine(char* cmd);

void handleHFlag();
int scanForCommand(char* cmd);
void handleHelp();
void handleLogout();
void handleListu(char* msg);

void readProtocolInput(char cmd[MAX_INPUT], int fd);

void wprintf(char* msg);
char** alloc2DArray(int rows, int length);
char** tokenize(char *cmd, int* length);
void printArray(char **array, int length);
void createIAmBuffer(char* iAm);
int validateMessage(char** msg, int msgLen, char* verb);
int handleProtocolInput(int socketfd, char* msg, int* len, char* verb);
int validateProtocolInput(char* msg, int* len, char* verb);
void printTime(char* msg);
void selectLoop();
void handleCommand(char cmd[MAX_INPUT]);

void wprintProtocol(char* msg);
int checkForChatCommand(char* cmd);
void handleMsgReceived(char* cmd);
int findGreatestChatFd();
void handleChatInput(char* msg, int chatfd);
void sendMsgToChat(char* msg);
void catchSigChild();
void catchSigQuit();
void closeAllChats();
void logout(char* msg, int* tokenLen);
void closeChatWindow(int fd);
void handleUoff(char* msg);
void writeVerbose(int socketfd, char* msg, int strLen);
void printVerbose(char* msg);
void printSent();
void printReceived();
void doLogout();
void printError(char** tokenizedMsg, int tokenLen);
int handleDisconnection(char* msg, int fd);
void promptPassword();
int isHistoryFD(int i);
int writeToChat(int chatfd, char* msg, int msgLen);
void printHistory(struct chat* temp);
void colorVerbose();
void colorDefault();
void colorError();

//HW6 forward declares
void writeAuditLogToTerminal();
void initAuditLog(char** argv);
void sfwriteTest(pthread_mutex_t *lock, FILE* stream, char *fmt, ...);
void writeTimestamp();
void writeLogEvent(char* event);
void writeMsgEvent(int from, char** tokenizedMsg, int tokenLen);
void writeLoginEvent(char* msg, int success);
void writeLogoutEvent(int intentional);
void writeErrorEvent(char* msg);
void writeCmdEvent(char* cmd, int success);

//Globals
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int hFlag = 0;
int cFlag = 0;
int vFlag = 0;
int aFlag = 0;

int auditFileLocation = 0;

int socketfd;
int unixfd;

struct addrinfo hints, *servinfo;

char port[MAX_INPUT];
char serverName[MAX_INPUT];
char username[MAX_INPUT];
char password[MAX_INPUT];

char cmd[MAX_INPUT]; //command string

struct chat *chat_head; //head of chat list 

int unix_socket_fd; //file descriptor of the Unix Domain Socket

char* wolfieBuffer = "WOLFIE \r\n\r\n";
char* eiflow = "EIFLOW";
char* iamBuffer = "IAM ";
char* endBuffer = "\r\n\r\n";

char* timeBuffer = "TIME \r\n\r\n";
char* byeBuffer = "BYE \r\n\r\n";
char* listuBuffer = "LISTU \r\n\r\n";
char** envpGlobal;

FILE* logFile;
FILE* stdFile;

//Chat List Functions

struct chat* createChat(char* title, char* name, struct chat* existingChat)
{
	struct chat* temp;
	if(existingChat==NULL)
		temp = (struct chat*)calloc(1, sizeof(*temp));
	else 
	{
		temp = existingChat;
		temp->disconnected = 0;
		temp->chatClosed = 0;
	}

	int socket_pair[2]; 
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) == -1)
	{
		perror("Failed to create chat (due to failure to create socket pair).");
		return NULL;
	}
	else
	{
		temp->fd = socket_pair[0];
		temp->passed_fd = socket_pair[1];
		strcpy(temp->name, name);

		char filename[MAX_INPUT];
		strcpy(filename, username);
		strcat(filename, name);
		strcat(filename, ".txt");

		//printf("Trying to open: %s\n", filename);
		temp->history_fd = open(filename, O_RDWR|O_CREAT|O_APPEND, 0644);
		//printf("Opened: %d\n", temp->history_fd);

		int pid = fork();
		if(pid==0)
		{
			char fds[50];
			memset(fds, 0, 50);
			snprintf(fds, 50, "%d", temp->passed_fd);


			char auditFdString[50];
			memset(auditFdString, 0, 50);
			int auditFd = fileno(logFile);
			snprintf(auditFdString, 50, "%d", auditFd);

			char* newArgv[9] = {"/usr/bin/xterm", "-T", title, "-e", "./chat", fds , auditFdString, username, 0};
			execvp(newArgv[0], newArgv);
			perror("Failure execvp xterm.");
		}

		temp->pid = pid;

		printHistory(temp);
		return temp;
	}
}

void printHistory(struct chat* temp)
{
	char* last_char = calloc(1,1);
	while(read(temp->history_fd, last_char, sizeof(last_char)>0))
	{
		write(temp->fd, last_char, strlen(last_char));
	}
	printf("\n");
	free(last_char);
}



void addToChats(struct chat* newChat)
{
	struct chat* curr = chat_head;

	if(curr == NULL)
	{
		chat_head = newChat;
	}
	else
	{
		while((*curr).next != NULL)
		{
			curr = (*curr).next;
		}
		(*curr).next = newChat;
	}
}

//returns pointer to the chat so you can free after your done
struct chat* removeChat(char *name)
{

	struct chat* curr = chat_head;

	if(curr == NULL)
	{
		return NULL;
	}
	else
	{
		if(strcmp((*curr).name, name) == 0)
		{
			//CHANGE THE HEAD TO THE NEXT
			//RETURN CURR
			chat_head = (*curr).next;
			return curr;
		}
		else
		{
			//NOT THE HEAD
			while((*curr).next != NULL)
			{
				if(strcmp( (*((*curr).next)).name, name) == 0)
				{
					struct chat* ret = (*curr).next;

					(*curr).next = (*((*curr).next)).next;		

					return ret;			
				}
				curr = (*curr).next;
			}
		}
	}

	return NULL;
}

struct chat* removeChatFD(int fd)
{
	struct chat* curr = chat_head;

	if(curr == NULL)
	{
		return NULL;
	}
	else{
		if((*curr).fd == fd)
		{
			//CHANGE THE HEAD TO THE NEXT
			//RETURN CURR
			chat_head = (*curr).next;
			return curr;
		}
		else
		{
			//NOT THE HEAD
			while((*curr).next != NULL)
			{
				if((*((*curr).next)).fd == fd)
				{
					struct chat* ret = (*curr).next;

					(*curr).next = (*((*curr).next)).next;		

					return ret;			
				}
				curr = (*curr).next;
			}
		}
	}

	return NULL;
}

struct chat* removeChatPid(int pid)
{
	struct chat* curr = chat_head;

	if(curr == NULL)
	{
		return NULL;
	}
	else{
		if((*curr).pid == pid)
		{
			//CHANGE THE HEAD TO THE NEXT
			//RETURN CURR
			//chat_head = (*curr).next;
			//printf("Closing chat: %d\n", pid);
			curr->disconnected = 1;
			curr->chatClosed = 1;
			return curr;
		}
		else
		{
			//NOT THE HEAD
			while((*curr).next != NULL)
			{
				if((*((*curr).next)).pid == pid)
				{
					struct chat* ret = (*curr).next;

					//(*curr).next = (*((*curr).next)).next;
					//printf("Closing chat: %d\n", pid);
					ret->disconnected = 1;
					ret->chatClosed = 1;	

					return ret;			
				}
				curr = (*curr).next;
			}
		}
	}

	return NULL;
}

struct chat* getChatByFd(int chatfd)
{
	struct chat* curr = chat_head;

	if(curr == NULL)
	{
		return NULL;
	}
	else
	{
		while(curr != NULL)
		{
			int currFd = curr->fd;

			if(currFd < 0)
			{
				//return NULL;
			}

			else
			{
				if(currFd == chatfd)
				{
					return curr;
				}
			}

			curr = (*curr).next;
		}		
	}
	return NULL;
}


struct chat* getChat(char *name)
{
	struct chat* curr = chat_head;

	if(curr == NULL)
	{
		return NULL;
	}
	else
	{
		while(curr != NULL)
		{
			char* currName = (*curr).name;

			if(currName == NULL)
			{
				//return NULL;
			}
			else
			{
				if(strcmp(currName, name) == 0)
				{
					return curr;
				}
			}

			curr = (*curr).next;
		}		
	}
	return NULL;
}

void printChat(struct chat* chat){
	if(chat == NULL){
		return;
	}

	wprintf("\n");

	wprintf("Name: ");
	wprintf((*chat).name);
	wprintf("\n");

	wprintf("I/O FD: ");
	int iofd = ((*chat).fd);
	char iofd_string[50];
	memset(iofd_string, 0, 50);
	snprintf(iofd_string, 50, "%d", iofd);
	write(1, iofd_string, sizeof(iofd_string));
	wprintf("\n");

	wprintf("Passed FD: ");
	int pfd = ((*chat).passed_fd);
	char pfd_string[50];
	memset(pfd_string, 0, 50);
	snprintf(pfd_string, 50, "%d", pfd);
	write(1, pfd_string, sizeof(pfd_string));
	wprintf("\n");

	printf("PID: %d\n", chat->pid);
	printf("Disconnected: %d\n", chat->disconnected);
	printf("Chat Closed: %d\n", chat->chatClosed);
}

void printChatList(){
	struct chat* curr = chat_head;

	wprintf("\n+=| Chat List |=+\n");

	if(curr == NULL){
		wprintf("\nList is empty\n");
	}
	else{
		while(curr != NULL){
			printChat(curr);
			curr = (*curr).next;
		}
	}

	wprintf("\n+=----------=+\n");
}
//Chat List Functions -- end

int main(int argc, char **argv, char** envp)
{

	//argv[2] = "localhost";
	//argv[3] = "4444";
	//argc = 4;

	signal(SIGINT, catchCtrlC);
	signal(SIGCHLD, catchSigChild);
	signal(SIGTERM, catchSigQuit);

	envpGlobal = envp;

	//Set the FILE for stdout
	//stdFile = fdopen(1, "w+");
	//printf("StdFile: %d\n", stdFile);

	setCommandFlags(argc, argv);
	initAuditLog(argv);

					
	//Init the client's socket	
	initSocket();
	sendConnectionRequest();
	//Log in with new user if h flag is set. Existing user otherwise
	if(cFlag)
		handleNewUserLoginProtocol();
	else handleLoginProtocol();

	//Don't exit
	selectLoop();
}

void initAuditLog(char** argv)
{
	if(aFlag==1)
	{
		printf("Trying to open audit file: %s\n", argv[auditFileLocation]);
		logFile = fopen(argv[auditFileLocation], "a+");
		//printf("Contents: %s", read)
		//sfwriteTest(&mutex, logFile, "Writingnums: %d, %f", 3, 3.15);
	}
	else
	{
		logFile = fopen("audit.log", "a+");
		printf("Default audit file opened\n");
	}
}

void sfwriteTest(pthread_mutex_t *lock, FILE* stream, char *fmt, ...){
	pthread_mutex_lock(lock);

	va_list ap;

	va_start(ap, fmt);
	vfprintf(stream, fmt, ap);
	va_end(ap);

	pthread_mutex_unlock(lock);
}

void promptPassword()
{
	char* prompt = "Enter Password: ";
	char* tempPass = getpass(prompt);

	strcpy(password, tempPass);
	//printf("Pass: %s\n",password);
}

void selectLoop()
{
	fd_set readfds;
	struct timeval timeout;

	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	char* cursor = cmd;
	char msg[MAX_INPUT];

	while(1)
	{
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		FD_SET(socketfd, &readfds);

		struct chat* tempChat = chat_head;

		while(tempChat!=NULL)
		{
			if(tempChat->chatClosed==0)
			{
				FD_SET(tempChat->fd, &readfds);
				FD_SET(tempChat->history_fd, &readfds);
			}
			
			//printf("added fd: %d\n", tempChat->fd);
			tempChat = tempChat->next;

		}

		int biggestFd = findGreatestChatFd();
		//printf("biggestFd: %d\n", biggestFd);
		int sret = select(biggestFd+1, &readfds, NULL, NULL, &timeout);
		//printf("sret %d\n", sret);
		
		for(int i=0;i<=biggestFd && sret > 0; i++)
		{
			int set = FD_ISSET(i, &readfds);
			if(set)
			{
				//Reading from server
				if(i == socketfd)
				{
					readProtocolInput(msg, socketfd);
				

					int tokenLen = 0;
					//printReceived();
					//printVerbose(msg);

					//Handle Shutdown
					if(validateProtocolInput(msg, &tokenLen, "BYE"))
					{
						writeLogoutEvent(1);
						closeAllChats();
						doLogout();
					}

					//Handle MSG
					if(validateProtocolInput(msg, &tokenLen, "MSG"))
					{
						printf("Received MSG: %s\n", msg);
						handleMsgReceived(msg);
						sendMsgToChat(msg);
					}

					if(validateProtocolInput(msg, &tokenLen, "UOFF"))
					{
						handleUoff(msg);
					}

					if(validateProtocolInput(msg, &tokenLen, "EMIT"))
					{
						//Receive EMIT <time> \r\n\r\n
						
							printTime(msg);
						
					}

					if(validateProtocolInput(msg, &tokenLen, "UTSIL"))
					{
						handleListu(msg);
					}

					if(validateProtocolInput(msg, &tokenLen, "ERR"))
					{
						writeErrorEvent(msg);
					}
				}

				//Reading from stdin
				else if(i == 0)
				{
					char last_char;
					int rv = read(0, &last_char, 1);

					//Failed to read from stdin
					if(!rv)
					{
						error("Error in reading from stdin");
					}

					//When Enter is pressed
					if(last_char == '\n')
					{
						*cursor = '\0';
						cursor = cmd;
						//execute command
						//write(1, cmd, strlen(cmd));
						//write(1, "\n", 1);
						handleCommand(cmd);
					}
					else
					{
						//append to current string
						*cursor = last_char;
						cursor++;
					}
					
				}

				else if(isHistoryFD(i))
				{
					//printf("History Input\n");
				}
				//Chat Input
				else
				{
					//Close if disconnected
					if(getChatByFd(i)->disconnected==1)
					{

						closeChatWindow(i);
						//printChatList();
					}
					else
					{
					readProtocolInput(msg, i);
					
					if(strcmp(msg, "CLOSING CHAT WINDOW \r\n\r\n") ==0)
					{
						writeCmdEvent("/close", 1);
					}
					else handleChatInput(msg, i);
					}
				}
			}
		}

	}

}

int isHistoryFD(int i)
{
	struct chat* tempChat;
	tempChat = chat_head;

	while(tempChat!=NULL)
	{
		if(tempChat->history_fd == i)
			return 1;
		tempChat = tempChat->next;
	}
	return 0;
}


void handleUoff(char* msg)
{
	int tokenLen = 0;
	//printf("Tokenizing: %s\n", msg);
	char** tokenizedMsg = tokenize(msg, &tokenLen);
	
	//printArray(tokenizedMsg, tokenLen);
	char* name = tokenizedMsg[1];

	struct chat* tempChat = getChat(name);

	if(tempChat!=NULL)
	{
		write(tempChat->fd, name, strlen(name));
		write(tempChat->fd, " has disconnected.\n", strlen(" has disconnected.\n"));
		tempChat->disconnected = 1;
	}

	free(tokenizedMsg);
}

void closeChatWindow(int fd)
{
	struct chat* tempChat = getChatByFd(fd);

	kill(tempChat->pid, SIGQUIT);

	tempChat = getChatByFd(fd);
	close(tempChat->fd);
	close(tempChat->passed_fd);

	printf("chat closed: %d\n", tempChat->pid);
	tempChat->chatClosed = 1;
}

void sendMsgToChat(char* msg)
{
	int tokenLen = 0;
	char** tokenizedMsg = tokenize(msg, &tokenLen);

	struct chat* tempChat;
	int chatFd = 0;

	if(strcmp(tokenizedMsg[1], username) == 0)
	{
		tempChat = getChat(tokenizedMsg[2]);
		chatFd = tempChat->fd;

		writeToChat(chatFd, "< ", strlen("< "));
	}
	else
	{
		tempChat = getChat(tokenizedMsg[1]);
		chatFd = tempChat->fd;

		writeToChat(chatFd, "> ", strlen("> "));
	}

	
	printf("Chat's fd: %d\n", chatFd);

	for(int i = 3; i < tokenLen-1;i++)
	{
		//printf("Tried to write to chat: %s\n", tokenizedMsg[i]);
		writeToChat(chatFd, tokenizedMsg[i], strlen(tokenizedMsg[i]));
		writeToChat(chatFd, " ", strlen(" "));
	}
	writeToChat(chatFd, "\n", strlen("\n"));


	free(tokenizedMsg);
}

int findGreatestChatFd()
{
	int biggestFd = socketfd;

	struct chat* tempChat = chat_head;
	while(tempChat!=NULL)
	{
		if(tempChat->fd > biggestFd && tempChat->chatClosed==0)
			biggestFd = tempChat->fd;
		tempChat = tempChat->next;
	}

	/*while(tempChat!=NULL)
	{
		if(tempChat->history_fd>biggestFd)
			biggestFd = tempChat->history_fd;
		tempChat = tempChat->next;
	}*/

	return biggestFd;
}

void handleMsgReceived(char* cmd)
{

	//Only spawn chat if we need to
	int tokenLen = 0;
	char** tokenizedMsg = tokenize(cmd, &tokenLen);
	char to_name[MAX_INPUT];
	memset(to_name, 0, MAX_INPUT);

	char name[MAX_INPUT];
	memset(name, 0, MAX_INPUT);

	char* chat_name;

	int from = 0;

	// /chat <to> <from> <msg>

	if(strcmp(tokenizedMsg[1], username) == 0)
	{ 
		strcat(to_name, username);
		strcat(to_name, " | Currently messaging ");
		strcat(to_name, tokenizedMsg[2]);
		chat_name = tokenizedMsg[2];
		from = 1;
	}
	else
	{
		strcat(to_name, username);
		strcat(to_name, " | Currently messaging ");
		strcat(to_name, tokenizedMsg[1]);
		chat_name = tokenizedMsg[1];
	}

	if(strcmp(tokenizedMsg[1], username) == 0)
	{
		strcpy(name, tokenizedMsg[2]);
		from = 1;
	}
	else
	{
		strcpy(name, tokenizedMsg[1]);
	}

	writeMsgEvent(from, tokenizedMsg, tokenLen);

	free(tokenizedMsg);

	if(getChat(chat_name)==NULL)
		addToChats(createChat(to_name, name, NULL));
	else if(getChat(chat_name)->disconnected==1)
	{
		//printf("Updating old chat info\n");
		createChat(to_name, name, getChat(chat_name));
	}
	else
	{
		//printf("No window. to_name: %s, getChat: getChat(to_name): %s\n", chat_name, getChat(chat_name)->name);
	}




}

void writeMsgEvent(int from, char** tokenizedMsg, int tokenLen)
{
	//From denotes if this is a from or to message

	char* messageStr = calloc(MAX_INPUT, sizeof(char*));

	strcat(messageStr, "MSG, ");
	if(from)
	{
		strcat(messageStr, "from, ");
		strcat(messageStr, tokenizedMsg[2]);
		strcat(messageStr, ", ");
	}

	else
	{
		strcat(messageStr, "to, ");
		strcat(messageStr, tokenizedMsg[1]);
		strcat(messageStr, ", ");
	}

	for(int i = 3; i < tokenLen-1;i++)
	{
		strcat(messageStr, tokenizedMsg[i]);
		strcat(messageStr, " ");
	}

	writeLogEvent(messageStr);

	free(messageStr);
}

void writeLoginEvent(char* msg, int success)
{
	char* messageStr = calloc(MAX_INPUT, sizeof(char*));

	strcat(messageStr, "LOGIN, ");
	strcat(messageStr, serverName);
	strcat(messageStr, ":");
	strcat(messageStr, port);
	strcat(messageStr, ", ");
	if(success==0)
		strcat(messageStr, "fail, ");
	else strcat(messageStr, "success, ");
	trimNewLine(msg);
	strcat(messageStr, msg);

	writeLogEvent(messageStr);

	free(messageStr);
}

void writeLogoutEvent(int intentional)
{
	char* messageStr = calloc(MAX_INPUT, sizeof(char*));

	strcat(messageStr, "LOGOUT, ");
	if(intentional==0)
		strcat(messageStr, "error");
	else strcat(messageStr, "intentional");

	writeLogEvent(messageStr);

	free(messageStr);
}

void writeErrorEvent(char* msg)
{
	char* messageStr = calloc(MAX_INPUT, sizeof(char*));

	strcat(messageStr, "ERR, ");
	trimNewLine(msg);
	strcat(messageStr, msg);

	writeLogEvent(messageStr);

	free(messageStr);
}

void writeCmdEvent(char* cmd, int success)
{
	char* messageStr = calloc(MAX_INPUT, sizeof(char*));

	strcat(messageStr, "CMD, ");

	trimNewLine(cmd);
	strcat(messageStr, cmd);
	strcat(messageStr, ", ");

	if(success==0)
		strcat(messageStr, "failure, ");
	else strcat(messageStr, "success, ");
	strcat(messageStr, "client");

	writeLogEvent(messageStr);

	free(messageStr);
}

void checkAndWriteCmdFail(char* cmd)
{
	//Check if a command is empty
	//if(strcmp(cmd, "\n")==0)
		//return;
	//else
		writeCmdEvent(cmd, 0);
}

void handleChatInput(char* msg, int chatfd)
{
	int tokenLen = 0;
	char** tokenizedMsg = tokenize(msg, &tokenLen);

	struct chat* tempChat = getChatByFd(chatfd);

	printSent();
	writeVerbose(socketfd, "MSG ", strlen("MSG "));
	writeVerbose(socketfd, tempChat->name, strlen(tempChat->name));
	writeVerbose(socketfd, " ", strlen(" "));
	writeVerbose(socketfd, username, strlen(username));
	writeVerbose(socketfd, " ", strlen(" "));
	//printf("msg is: %s\n", msg);
	writeVerbose(socketfd, msg, strlen(msg));
	//printf("msg verification: %s\n", msg);
	writeVerbose(socketfd, " ", strlen(" "));
	writeVerbose(socketfd, endBuffer, strlen(endBuffer));

	free(tokenizedMsg);

}

void handleCommand(char cmd[MAX_INPUT])
{
	scanForCommand(cmd);
}
	
//Returns 0 if no command found. 1 for time, 2 for /help, 3 for logout, 4 for /listu
int scanForCommand(char* cmd)
{
	char msg[MAX_INPUT];
	int tokenLen = 0;

	if(strcmp(cmd, "\0")==0)
		return 0;
	if(strcmp(cmd, "/time")==0)
	{
		//printf("Command recognized: time\n");
		//Send TIME \r\n\r\n
		writeCmdEvent(cmd, 1);
		printSent();
		writeVerbose(socketfd, timeBuffer, strlen(timeBuffer));

		return 1;
	}
	if(strcmp(cmd, "/help")==0)
	{
		//printf("Command recognized: help\n");
		writeCmdEvent(cmd, 1);
		handleHelp();
		return 2;
	}
	if(strcmp(cmd, "/logout")==0)
	{
		//printf("Command recognized: logout\n");
		writeCmdEvent(cmd, 1);
		logout(msg, &tokenLen);
		return 3;
	}
	if(strcmp(cmd, "/listu")==0)
	{
		//printf("Command recognized: listu\n");
		//printf("Listubuffer: %d\n", listuBuffer[0]);
		writeCmdEvent(cmd, 1);
		printSent();
		writeVerbose(socketfd, listuBuffer, strlen(listuBuffer));

		return 4;
	}
	if(strcmp(cmd, "/listc") == 0)
	{
		writeCmdEvent(cmd, 1);
		printChatList();
		return 5;
	}
	if(strcmp(cmd, "/audit") == 0)
	{
		writeCmdEvent(cmd, 1);
		writeAuditLogToTerminal();
		return 6;
	}
	if(strcmp(cmd, "/log") == 0)
	{
		writeLogEvent("TestEvent");
		return 7;
	}

	int chatCommand = checkForChatCommand(cmd);

	if(chatCommand==0)
	{
		checkAndWriteCmdFail(cmd);
	}
	else writeCmdEvent("/chat", 1);

	return 0;
}

void writeAuditLogToTerminal()
{
	char* last_char = calloc(1,1);
	int auditFd = fileno(logFile);

	time_t currTime = time(NULL);
	printf("Time: %s",asctime(localtime(&currTime)));

	rewind(logFile);

	flock(auditFd, LOCK_EX);
	while(read(auditFd, last_char, sizeof(last_char)>0))
	{
		//printf("trying\n");
		sfwrite(&mutex, stdout, "%s", last_char);
	}

	flock(auditFd, LOCK_UN);
	free(last_char);
}

void writeLogEvent(char* event)
{
	int fd = fileno(logFile);
	flock(fd, LOCK_EX);

	writeTimestamp();

	sfwrite(&mutex, logFile, ", %s", username);
	sfwrite(&mutex, logFile, ", %s\n", event);

	fflush(logFile);

	flock(fd, LOCK_UN);
}

void writeTimestamp()
{
	char outstr[200];
    time_t t;
    struct tm *tmp;

   t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
        perror("localtime");
        exit(EXIT_FAILURE);
    }

   if (strftime(outstr, sizeof(outstr), "%D-%I:%M%P", tmp) == 0) {
        fprintf(stderr, "strftime returned 0");
        exit(EXIT_FAILURE);
    }
	sfwrite(&mutex, logFile, "%s", outstr);

}

void logout(char* msg, int* tokenLen)
{
	//Send BYE \r\n\r\n
	printSent();
	writeVerbose(socketfd, byeBuffer, strlen(byeBuffer));

	//Receive BYE \r\n\r\n
	int bye = handleProtocolInput(socketfd, msg, tokenLen, "BYE");

	writeLogoutEvent(1);

	//Close connection
	if(bye)
	{
		closeAllChats();
		doLogout();
	}
}

void closeAllChats()
{
	struct chat* tempChat = chat_head;

	while(tempChat!=NULL)
	{
		kill(tempChat->pid, SIGQUIT);

		close(tempChat->fd);
		close(tempChat->passed_fd);
		close(tempChat->history_fd);
		tempChat = tempChat->next;
	}
}

int checkForChatCommand(char* cmd)
{
	int commandFound = 0;
	int tokenLen = 0;
	char** tokenizedMsg = tokenize(cmd, &tokenLen);

	if(strcmp(tokenizedMsg[0], "/chat")==0)
	{
		commandFound = 1;
		if(strcmp(tokenizedMsg[1], username) == 0){
			wprintf("You cannot chat with yourself.\n");
		}
		else{
			printSent();
			writeVerbose(socketfd, "MSG ", strlen("MSG "));
			writeVerbose(socketfd, tokenizedMsg[1], strlen(tokenizedMsg[1]));
			writeVerbose(socketfd, " ", strlen(" "));
			writeVerbose(socketfd, username, strlen(username));
			writeVerbose(socketfd, " ", strlen(" "));
			for(int i = 2; i < tokenLen;i++)
			{
				writeVerbose(socketfd, tokenizedMsg[i], strlen(tokenizedMsg[i]));
				writeVerbose(socketfd, " ", strlen(" "));
			}
			writeVerbose(socketfd, " ", strlen(" "));
			writeVerbose(socketfd, endBuffer, strlen(endBuffer));
		}
	}
	free(tokenizedMsg);

	return commandFound;
}

void printTime(char* msg)
{
	int tokenLen = 0;
	char** tokenizedMsg = tokenize(msg, &tokenLen);

	long time = strtol(tokenizedMsg[1], NULL, 10);
	
	long seconds = time % 60;
	time = time - seconds;
	long minutes = (time % 3600)/60;
	time = time - minutes*60;
	long hours = time / 3600;
	printf("Hours %lu, Minutes: %lu, Seconds: %lu \n", hours, minutes, seconds);

	free(tokenizedMsg);
}

void setCommandFlags(int argc, char **argv)
{
	//Set flags
	parseOpt(argc, argv);

	//Handle -h flag here, or we might exit because of too few args
	if(hFlag)
		handleHFlag();

	//Not enough arguments (NAME, SERVER, PORT at least)
	if(argc<4)
	{
		fprintf(stderr, "Name, Server, or Port not specified\n");
		exit(EXIT_FAILURE);
	}

	//Set Server and port
	//printf("Argc: %d\n", argc);

	strcpy(username, argv[argc-3]);
	strcpy(serverName,argv[argc-2]);
	strcpy(port, argv[argc-1]);
	printf("Username Set: %s\n", username);
	printf("Server Set: %s\n", serverName);
	printf("Port Set: %s\n", port);


}

void initSocket()
{
	int returnValue;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	printf("Server Name: %s\n", serverName);
	if ((returnValue = getaddrinfo(serverName, port, &hints, &servinfo)) != 0) 
	{
	    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(returnValue));
	    exit(1);
	}

}

void sendConnectionRequest()
{

	struct addrinfo *addrLoop;

	// connect to first one we find
	for(addrLoop = servinfo; addrLoop != NULL; addrLoop = addrLoop->ai_next) 
	{
	    if ((socketfd = socket(addrLoop->ai_family, addrLoop->ai_socktype, addrLoop->ai_protocol)) == -1) 
	    {
	        perror("socket");
	        continue;
    	}

	    if (connect(socketfd, addrLoop->ai_addr, addrLoop->ai_addrlen) == -1) 
	    {
	        perror("connect");
	        doLogout();
	        continue;
	    }

	    printf("Connected\n\n");
	    break; // Successful connection if we're here
	}

	if (addrLoop == NULL) 
	{
	    // Reached end of list. No connection found.
	    fprintf(stderr, "No connection found\n");
	    exit(2);
	}

	//Free the servinfo
	freeaddrinfo(servinfo);
}

void handleLoginProtocol()
{
	//Message we receive
	char msg[MAX_INPUT];
	int tokenLen = 0;

	//Send WOLFIE \r\n\r\n
	printSent();
	writeVerbose(socketfd, wolfieBuffer, strlen(wolfieBuffer));

	//Receive EIFLOW \r\n\r\n
	handleProtocolInput(socketfd, msg, &tokenLen, "EIFLOW");
	
	//Send IAM <name> \r\n\r\n
	char iAm[strlen(username)+ strlen("IAM ") + strlen("\r\n\r\n")];
	createIAmBuffer(iAm);
	printSent();
	writeVerbose(socketfd, iAm, strlen(iAm));

	//Receive AUTH \r\n\r\n
	int auth = handleProtocolInput(socketfd, msg, &tokenLen, "AUTH");

	//Check for user account error OR User already logged in
	if(auth==0)
	{
		validateProtocolInput(msg, &tokenLen, "ERR");
		writeLoginEvent(msg, 0);
		handleProtocolInput(socketfd, msg, &tokenLen, "BYE");
		doLogout();
		return;
	}

	promptPassword();

	//Send PASS <password> \r\n\r\n
	printSent();
	writeVerbose(socketfd, "PASS ", strlen("PASS "));
	writeVerbose(socketfd, password, strlen(password));
	writeVerbose(socketfd, " ", strlen(" "));
	writeVerbose(socketfd, endBuffer, strlen(endBuffer));

	//Receive SSAP \r\n\r\n
	int ssap = handleProtocolInput(socketfd, msg, &tokenLen, "SSAP");

	//Check for bad password
	if(ssap==0)
	{
		validateProtocolInput(msg, &tokenLen, "ERR");
		writeLoginEvent(msg, 0);
		handleProtocolInput(socketfd, msg, &tokenLen, "BYE");
		doLogout();
		return;
	}

	///
	//Receive HI <name> \r\n\r\n
	int receivedHi = handleProtocolInput(socketfd, msg, &tokenLen, "HI");


	//Receive MOTD On success
	if(receivedHi)
	{
		handleProtocolInput(socketfd, msg, &tokenLen, "MOTD");

		//Print the MOTD
		writeLoginEvent(msg, 1);
		printf("%s", msg);
	}
	//Begin bye protocol
	else
	{
		//Check for ERR
		char** tokenizedMsg = tokenize(msg, &tokenLen);
		int returnV = validateMessage(tokenizedMsg, tokenLen, "ERR");
		free(tokenizedMsg);

		if (returnV==1)
		{
			printf("ERR Received\n");
			//Receive BYE Verb
			if(handleProtocolInput(socketfd, msg, &tokenLen, "BYE"))
			{
				writeLoginEvent(msg, 0);
				printSent();
				writeVerbose(socketfd, byeBuffer, strlen(byeBuffer));
				closeAllChats();
				doLogout();
				
			}
		}

	}
}

void handleNewUserLoginProtocol()
{
	//Message we receive
	char msg[MAX_INPUT];
	int tokenLen = 0;

	//Send WOLFIE \r\n\r\n
	printSent();
	writeVerbose(socketfd, wolfieBuffer, strlen(wolfieBuffer));

	//Receive EIFLOW \r\n\r\n
	handleProtocolInput(socketfd, msg, &tokenLen, "EIFLOW");
	
	//Send IAMNEW <name> \r\n\r\n
	printSent();
	writeVerbose(socketfd,"IAMNEW ", strlen("IAMNEW "));
	writeVerbose(socketfd, username, strlen(username));
	writeVerbose(socketfd, " ", strlen(" "));
	writeVerbose(socketfd, endBuffer, strlen(endBuffer));

	//Receive HINEW <name> \r\n\r\n
	int hinew = handleProtocolInput(socketfd, msg, &tokenLen, "HINEW");

	//Check for ERR 00 USER NAME TAKEN
	if(hinew==0)
	{
		validateProtocolInput(msg, &tokenLen, "ERR");
		writeLoginEvent(msg, 0);
		handleProtocolInput(socketfd, msg, &tokenLen, "BYE");
		doLogout();
		return;
	}

	promptPassword();

	//Send NEWPASS <password> \r\n\r\n
	printSent();
	writeVerbose(socketfd, "NEWPASS ", strlen("NEWPASS "));
	writeVerbose(socketfd, password, strlen(password));
	writeVerbose(socketfd, " ", strlen(" "));
	writeVerbose(socketfd, endBuffer, strlen(endBuffer));

	//Receive SSAPWEN \r\n\r\n
	int ssapwen = handleProtocolInput(socketfd, msg, &tokenLen, "SSAPWEN");

	//Check for ERR 02 BAD PASSWORD
	if(ssapwen==0)
	{
		validateProtocolInput(msg, &tokenLen, "ERR");
		writeLoginEvent(msg, 0);
		handleProtocolInput(socketfd, msg, &tokenLen, "BYE");
		doLogout();
		return;
	}
	//Receive HI <name> \r\n\r\n
	int receivedHi = handleProtocolInput(socketfd, msg, &tokenLen, "HI");

	//Receive MOTD On success
	if(receivedHi)
	{
		handleProtocolInput(socketfd, msg, &tokenLen, "MOTD");

		//Print the MOTD
		writeLoginEvent(msg, 1);
		printf("%s", msg);
	}
	//Begin bye protocol
	else
	{
		//Check for ERR
		char** tokenizedMsg = tokenize(msg, &tokenLen);
		int returnV = validateMessage(tokenizedMsg, tokenLen, "ERR");
		free(tokenizedMsg);

		if (returnV==1)
		{
			printf("ERR Received\n");
			//Receive BYE Verb
			if(handleProtocolInput(socketfd, msg, &tokenLen, "BYE"))
			{
				printSent();
				writeLoginEvent(msg, 0);
				writeVerbose(socketfd, byeBuffer, strlen(byeBuffer));
				closeAllChats();
				doLogout();
				
			}
		}

	}
}

int handleProtocolInput(int socketfd, char* msg, int* len, char* verb)
{
	int tokenLen = 0;
	readProtocolInput(msg, socketfd);
	char** tokenizedMsg = tokenize(msg, &tokenLen);
	//printArray(tokenizedMsg, tokenLen);
	//printf("msg[0]s %s\n", tokenizedMsg[0]);
	int returnV = validateMessage(tokenizedMsg, tokenLen, verb);
	free(tokenizedMsg);

	if(returnV==1)
	{
		printReceived();
		printVerbose(msg);
	}

	return returnV;
}

void printError(char** tokenizedMsg, int tokenLen)
{
	for(int i = 1; i < tokenLen-1; i++)
	{
		printf("%s ", tokenizedMsg[i]);
	}
	printf("\n\n");
}

int validateProtocolInput(char* msg, int* len, char* verb)
{
	int tokenLen = 0;
	char** tokenizedMsg = tokenize(msg, &tokenLen);
	int returnV = validateMessage(tokenizedMsg, tokenLen, verb);

	if(returnV==1)
	{
		printReceived();
		printVerbose(msg);
	}
	if(validateMessage(tokenizedMsg, tokenLen, "ERR")==1 && strcmp(verb, "ERR")==0)
	{
		colorError();
		printf("Error: ");
		printError(tokenizedMsg, tokenLen);
		colorDefault();
	}

	free(tokenizedMsg);
	return returnV;
}

void createIAmBuffer(char* iAm)
{
	strcpy(iAm, iamBuffer);
	strcat(iAm, username);
	strcat(iAm, " ");
	strcat(iAm, "\r\n\r\n");
}

int validateMessage(char** msg, int msgLen, char* verb)
{
	//printf("Verb: %s, Terminate: %s\n", msg[0], msg[msgLen-1]);
	
	//Check if verb is wrong
	if(strcmp(msg[0], verb)!=0)
	{
		//printf("Wrong Message: %s\n", msg[0]);
		return 0;
	}
	//Check if final token is endBuffer
	if(strcmp(msg[msgLen-1], endBuffer)!=0)
	{
		//printf("Wrong Termination: %s\n", msg[0]);
		return 0;
	}

	//printf("Right Message: %s\n", msg[0]);
	return 1;
}

void readInput(char cmd[MAX_INPUT], char* prompt)
{
    char *cursor;
    char last_char;
    int rv;
    int count;


    // Print the prompt
    rv = write(1, prompt, strlen(prompt));
    if (!rv) { 
      return;
    }
      
    // read and parse the inpu
    for(rv = 1, count = 0, 
    cursor = cmd, last_char = 1;
    rv 
    && (++count < (MAX_INPUT-1))
    && (last_char != '\n');
    cursor++) { 

      rv = read(0, cursor, 1);
      last_char = *cursor;
    } 
    *cursor = '\0';

    if (!rv) { 
      return;
    }

    //Get rid of any newline characters
    trimNewLine(cmd);
    trimNewLine(cmd);
    //write(1, cmd, strnlen(cmd, MAX_INPUT));
}

void readProtocolInput(char cmd[MAX_INPUT], int fd){

	memset(cmd, 0, MAX_INPUT);
    char *cursor;
    int rv = 1;
    int count;
    char* terminationPointer;
    int finished =0;

    
    
    // read and parse the inpu
    for(rv = 1, count = 0, cursor = cmd; finished==0 && rv && (++count < (MAX_INPUT-1)); cursor++) 
    { 
   		//printf("Count First: %d\n", count);
	    rv = read(fd, cursor, 1);
	    //printf("RV: %d\n", rv);

	    //printf("Count: %d\n", count);
	    //printf("Val: %d\n", cmd[count-1]);

	    terminationPointer = cursor - 3;
	    if(*(terminationPointer) == '\r' && *(terminationPointer+1) == '\n' && *(terminationPointer+2) == '\r' && *(terminationPointer+3) == '\n')
	   	{
	   		//printf("\n\nWE FOUND IT\n\n");
	   		finished = 1;
	   	}

	   	//printf("%d %d %d %d\n", *(terminationPointer), *(terminationPointer+1), *(terminationPointer+2), *(terminationPointer+3));

	   	/*if(*(terminationPointer)==13)
	   		printf("%s", "/r");
	   	else if(*(terminationPointer)==10)
	   		printf("%s", "/n");
	   	else printf("%c", *(terminationPointer));

	   	if(*(terminationPointer+1)==13)
	   		printf("%s", "/r");
	   	else if(*(terminationPointer+1)==10)
	   		printf("%s", "/n");
	   	else printf("%c", *(terminationPointer+1));

	   	if(*(terminationPointer+2)==13)
	   		printf("%s", "/r");
	   	else if(*(terminationPointer+2)==10)
	   		printf("%s", "/n");
	   	else printf("%c", *(terminationPointer+2));

	   	if(*(terminationPointer+3)==13)
	   		printf("%s", "/r");
	   	else if(*(terminationPointer+3)==10)
	   		printf("%s", "/n");
	   	else printf("%c", *(terminationPointer+3));

	   	printf("\n");*/

    } 
    
    *cursor = '\0';

    //wprintProtocol(cmd);
    // Execute the command, handling built-in commands separately 
    // Just echo the command line for now
    //write(1, cmd, strnlen(cmd, MAX_INPUT));

    //printf("Finished Reading Protocol Input\n");
    handleDisconnection(cmd, fd);
}

int handleDisconnection(char* msg, int fd)
{
	if(fd!=socketfd)
		return 0;

	if(msg[0]=='\0')
	{
		writeLogoutEvent(0);
		doLogout();
		return 1;
	}
	return 0;
}

void wprintProtocol(char* msg)
{
	wprintf("WPRINTPROTOCOL ");
	for(int i = 0; i < strlen(msg); i++)
	{
		if(msg[i]==13)
		{
	   		printf("%s\n", "/r");
		}
	   	else if(msg[i]==10)
	   	{
	   		printf("%s\n", "/n");
	   	}
	   	else 
   		{
   			printf("%c\n", msg[i]);
   		}

	}
}

void error(char* message)
{
	fprintf(stderr,"%s\n", message);
	catchCtrlC();
	exit(0);
}

void catchCtrlC()
{
	printf("\nEnd with peace and harmony.\n");
	//clearUserList();
	writeLogoutEvent(0);
	doLogout();
	exit(0);
}

void catchSigChild(int sig)
{
  pid_t pid;

  pid = wait(NULL);

  //free(removeChatPid(pid));
  //printChatList();

  removeChatPid(pid);

  //printChatList();

  //printf("Pid %d exited.\n", pid);
}

void catchSigQuit()
{
	writeLogoutEvent(0);
	doLogout();
}
void trimNewLine(char* cmd)
{
  //Trim the newline character
  int i;
  for(i=0; i<strlen(cmd); i++)
  {
     if(cmd[i]=='\n')   
         cmd[i]='\0';
  }
}

void parseOpt(int argc, char **argv)
{
	int opt = EXIT_FAILURE;
	/* Parse short options */
    while((opt = getopt(argc, argv, "hcva")) != -1) {
        switch(opt) {
            case 'h':
            	hFlag=1;
            	printf("hFlag set\n");
            	break;

            case 'c':
            	cFlag=1;
            	printf("cFlag set\n");
            	break;

            case 'v':
            	vFlag=1;
            	printf("vFlag set\n");
            	break;
            case 'a':
            	aFlag=1;
            	auditFileLocation = optind;
            	printf("aFlag set\n");
            	printf("Optind: %d\n", optind);
            	break;
            case '?':
                // Let this case fall down to default; handled during bad option.
                 
            default:
                // A bad option was provided. 
            	printf("BAD OPTION\n");
                exit(EXIT_FAILURE);
                break;
        }
    }
}

void handleHFlag()
{
	printf("Usage: ./client [-hcv] [-a FILE] NAME SERVER_IP SERVER_PORT\n");
	printf("-a FILE\t\t\tPath to the audit log file.\n");
	printf("-h\t\t\tDisplays this help menu, and returns EXIT_SUCCESS\n");
	printf("-c\t\t\tRequests to server to create a new user\n");;
	printf("-v\t\t\tVerbose print all incoming and outgoing protocol verbs & content.\n");
	printf("NAME\t\t\tThis is the username to display when chatting\n");
	printf("SERVER_IP\t\tThe ipaddress of the server to connect to.\n");
	printf("SERVER_PORT\t\tThe port to connect to.\n");
	exit(EXIT_SUCCESS);
}

void handleHelp()
{
	printf("\nAccepted Commands\n");
	printf("/time\t\t\tAsks the server how long the client has been connected\n");
	printf("/help\t\t\tLists all commands the client accepts\n");
	printf("/logout\t\t\tDisconnects client with server\n");
	printf("/listu\t\t\tAsks the server who has been connected\n");
	printf("/audit\t\t\tDumps the content of audit.log to the client terminal\n");
}
void wprintf(char* msg)
{
	write(1, msg, strlen(msg));
}

char** alloc2DArray(int rows, int length){
	char **array = (char**)calloc(rows, sizeof(char*));

	for(int i=0;i<rows;i++){
		array[i] = (char*)calloc(length, sizeof(char));
	}

	return array;
}

char** tokenize(char *cmd, int* length)
{
	char **newArgv = alloc2DArray(100, 100);
  
    char* tempstr = calloc(strlen(cmd)+1, sizeof(char));
    strcpy(tempstr, cmd);

    char* temp = strtok(tempstr, " ");
    strcpy(newArgv[*length], temp);
    (*length)++;

    while((temp = strtok(NULL," ")) != NULL){
      strcpy(newArgv[*length], temp);
      (*length)++;
    }

    free(tempstr);

    return newArgv;
}
void printArray(char **array, int length)
{	
	for(int i=0;i<length;i++){
		if(array[i] == NULL){
			printf("%i | (null)\n", i);	
		}
		else{
			printf("%i | %s\n", i, array[i]);
		}
	}
}

void handleListu(char* msg)
{
	//printf("msg: %s\n", msg);
	int tokenLen = 0;
	char** tokenizedMsg = tokenize(msg, &tokenLen);
	printf("User List:\n");
	for(int i = 1; i < tokenLen-1; i++)
	{
		printf("%s", tokenizedMsg[i]);
	}
	printf("\n");
	free(tokenizedMsg);
}

void writeVerbose(int socketfd, char* msg, int strLen)
{
	write(socketfd, msg, strLen);
	if(vFlag)
	{
		colorVerbose();
		printf("%s", msg);
		colorDefault();
	}
}
void printVerbose(char* msg)
{
	if(vFlag)
	{
		colorVerbose();
		printf("%s", msg);
		colorDefault();
	}
}
void printSent()
{

	if(vFlag)
	{
		colorVerbose();
		printf("Sent: ");
		colorDefault();
	}
}
void printReceived()
{
	if(vFlag)
	{
		colorVerbose();
		printf("Received: ");
		colorDefault();
	}
}

void doLogout()
{
	closeAllChats();
	close(socketfd);
	fclose(logFile);
	printf("Disconnected\n");
	exit(EXIT_SUCCESS);

}

int writeToChat(int chatfd, char* msg, int msgLen)
{

	write(chatfd, msg, msgLen);
	int returnV = write(getChatByFd(chatfd)->history_fd, msg, msgLen);

	//printf("Wrote to chat: %d\n", returnV);

	return returnV;
}

void colorVerbose()
{
	write(1, "\x1B[1;34m", strlen("\x1B[1;34m"));
}

void colorDefault()
{
	write(1, "\x1B[0m", strlen("\x1B[0m"));
}

void colorError()
{
	write(1, "\x1B[1;31m", strlen("\x1B[1;31m"));
}

