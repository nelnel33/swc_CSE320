#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h> 
#include <pthread.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <fcntl.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#define MAX_INPUT 1024

typedef struct user{
	int connected;
	char ip_address[20];
	char name[MAX_INPUT];
	int client_socket_fd;
	pthread_t thread_id;
	time_t time_created;
	char password[MAX_INPUT];
	char salt[50];

	struct user* next;
} user;

int port; //port number
char* motd; //message of the day
int verbose = 0; // -v
char* accounts_file;

sqlite3 *database;

int socketfd; //server socket file descriptor

char cmd[MAX_INPUT]; //command string

struct sockaddr_in server_addr; //struct for bind function

//LIST OF LOGIN THREADS
struct user *head = NULL;

//Possible Messages
char* TERMINATION = "\r\n\r\n";
char* SPACE = " ";

char* WOLFIE = "WOLFIE";	
char* EIFLOW = "EIFLOW";	
char* IAMNEW = "IAMNEW";	
char* HINEW = "HINEW";		
char* NEWPASS = "NEWPASS";	
char* SSAPWEN = "SSAPWEN"; 	
char* HI = "HI";			
char* MOTD = "MOTD";		
char* BYE = "BYE";
char* IAM = "IAM";
char* AUTH = "AUTH";
char* PASS = "PASS";
char* SSAP = "SSAP";

//commands
char* EMIT = "EMIT";
char* UTSIL = "UTSIL";


//Error Messages
char* ERR00 = "ERR 00 USER NAME TAKEN \r\n\r\n";
char* ERR01 = "ERR 01 USER NOT AVAILABLE \r\n\r\n";
char* ERR02 = "ERR 02 BAD PASSWORD \r\n\r\n";
char* ERR100 = "ERR 100 INTERNAL SERVER ERROR \r\n\r\n";

/*Beginning of Forward Declarations*/
int getCommands(int argc, char** argv);
void printHelpMenu();
void closeProperly(int signo);
void initSocket();
void helpCommand();
void shutdownCommand();
void usersCommand();
void readInput(char cmd[MAX_INPUT], int fd);
void selectLoop();
void handleCommand(char *cmd);

void error();
void catchCtrlC();
void wprintf(char* msg);
void printArray(char **array, int length);
char** alloc2DArray(int rows, int length);
char** tokenize(char *cmd, int* length);
int validateMessage(char** msg, int msgLen, char* verb);

int checkClientCrash(struct user* client, char *cmsg);
void handleClientCrash(struct user* client);
void verbosePrint(char *message);
void disconnectUser(struct user* user);

//list functions
struct user* createUser(
	int connected,
	char *ip,
	char* name, 
	int client_fd,
	pthread_t thread_id,
	time_t time_created,
	char* password,
	char* salt
	);

void printUser(struct user* user);
void addUserList(struct user* user);
struct user* removeUserList();
void clearUserList();
void printUserList();
time_t getElapsedTime(time_t prev_time);
int handleClientCommands(struct user* currConnecting);
struct user* removeUserListByFD(int fd);
void broadcastToUsers(char* cmsg);
struct user* getUser(char* name);

int validPassword(char* pass);

void initDatabase();
void addUserToDatabase(struct user* user);
void retrieveUsersFromDatabase();

/*END OF FORWARD DECLARATIONS*/

void error(char* message){
	//fprintf(stderr,"%s\n", message);
	write(2, message, strlen(message));
	catchCtrlC();
	exit(0);
}

int hashPassword(char *password) {
    unsigned char temp[SHA256_DIGEST_LENGTH];
    char hashedPassword[SHA256_DIGEST_LENGTH * 2];
    
    SHA256((unsigned char*)password, strlen(password), temp);    
 
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
         sprintf(&hashedPassword[i], "%02x", (int)temp[i]);
    	//printf("i = %i", i);
 	}

 	//printf("hashedPassword: %s\n", hashedPassword);

 	if(SHA256_DIGEST_LENGTH <= MAX_INPUT){
 		strcpy(password, hashedPassword);
 		return 1;
 	}
 	else{
 		return 0;
 	}    
}

void generateSalt(char *salt) {
	char tmp[16];
	RAND_bytes((unsigned char*)tmp, 15);
	tmp[15] = (char)0;
	int c = 0;
    for(int i = 0; i < 7; i++) {
         sprintf(&salt[c++], "%02x", (int)tmp[i]);
 	}
 	salt[8] = (char)0;
}

//-1 if crash, 0 if no crash
int checkClientCrash(struct user* client, char *cmsg){
	if(cmsg == NULL){
		handleClientCrash(client);
		pthread_exit(NULL);
		return -1;
	}
	else if(cmsg[0] == '\0'){
		handleClientCrash(client);
		pthread_exit(NULL);
		return -1;
	}
	else{
		return 0;
	}
}

void handleClientCrash(struct user* client){
	struct user* temp = client;
	//removeUserListByFD(client->client_socket_fd);
	//if(client->name == NULL){
	//	client = removeUserListByFD(client->client_socket_fd);
	//}
	//else{
		client = getUser(client->name);
	//}

	if(client == NULL){
		client = temp;
	}

	disconnectUser(client);


	close(client->client_socket_fd);

	char uoff[MAX_INPUT];
	memset(uoff, 0, MAX_INPUT);

	strcat(uoff, "UOFF ");
	strcat(uoff, client->name);
	strcat(uoff, " \r\n\r\n");

	broadcastToUsers(uoff);
	verbosePrint(uoff);
	
}

void verbosePrint(char *message){
	if(verbose){
		write(1, message, strlen(message));
	}
}

void catchCtrlC(){
	if(verbose){
		printf("\n\nShutdown Server Socket FD: %d \n", socketfd);
	}
	int x = shutdown(socketfd, SHUT_RDWR);
	if(verbose){
		printf("Close/Shutdown Error on Server: %i\n\n", x);
		printf("End with peace and harmony.\n");
	}
	clearUserList();
	//int x = close(socketfd);
	exit(0);
}

time_t getElapsedTime(time_t prev_time){
	time_t currTime = time(NULL);
	return currTime - prev_time;
}

/*Beginning of List Functions*/
struct user* createUser(
	int connected,
	char *ip,
	char* name, 
	int client_fd,
	pthread_t thread_id,
	time_t time_created,
	char* password,
	char* salt
	){

	struct user* temp = (struct user*)calloc(1, sizeof(*temp));

	if(ip != NULL){
		strcpy((*temp).ip_address, ip);
	}
	else{
		(*temp).ip_address[0] = '\0';
	}

	if(name != NULL){
		strcpy((*temp).name, name);
	}
	else{
		(*temp).name[0] = '\0';
	}

	(*temp).client_socket_fd = client_fd;
	(*temp).thread_id = thread_id;

	(*temp).time_created = time_created;

	if(password != NULL){
		strcpy((*temp).password, password);
	}
	else{
		(*temp).password[0] = '\0';
	}

	if(salt != NULL){
		strcpy((*temp).salt, salt);
	}
	else{
		(*temp).salt[0] = '\0';
	}

	(*temp).next = NULL;

	return temp;
}

void printUser(struct user* user){
	if(user == NULL){
		return;
	}

	wprintf("\n");

	wprintf("Name: ");
	wprintf((*user).name);
	wprintf("\n");

	wprintf("IP Address: ");
	wprintf((*user).ip_address);
	wprintf("\n");

	wprintf("Client Socket FD: ");
	int csfd = ((*user).client_socket_fd);
	char csfd_string[50];
	memset(csfd_string, 0, 50);
	snprintf(csfd_string, 50, "%d", csfd);
	write(1, csfd_string, sizeof(csfd_string));
	wprintf("\n");

	wprintf("Is Client Connected? ");
	int conn = ((*user).connected);
	char conn_string[50];
	memset(conn_string, 0, 50);
	snprintf(conn_string, 50, "%d", conn);
	write(1, conn_string, sizeof(conn_string));
	wprintf("\n");


}

void addUserList(struct user* user){

	struct user* curr = head;

	if(curr == NULL){
		head = user;
	}
	else{
		while((*curr).next != NULL){
			curr = (*curr).next;
		}
		(*curr).next = user;
	}

}

struct user* getUser(char* name){
	struct user* curr = head;

	if(curr == NULL){
		return NULL;
	}
	else{
		while(curr != NULL){
			char* currName = (*curr).name;

			if(currName == NULL){
				//return NULL;
			}
			else{
				if(strcmp(currName, name) == 0){
					return curr;
				}
			}

			curr = (*curr).next;
		}		
	}

	return NULL;

}

struct user* getUserByFD(int fd){
	struct user* curr = head;

	if(curr == NULL){
		return NULL;
	}
	else{
		while(curr != NULL){
			char* currName = (*curr).name;

			if(currName == NULL){
				//return NULL;
			}
			else{
				if(curr->client_socket_fd == fd){
					return curr;
				}
			}

			curr = (*curr).next;
		}		
	}

	return NULL;
}

struct user* removeUserList(char* name){
	struct user* curr = head;

	if(curr == NULL){
		return NULL;
	}
	else{
		if(strcmp((*curr).name, name) == 0){
			//CHANGE THE HEAD TO THE NEXT
			//RETURN CURR
			head = (*curr).next;
			return curr;
		}
		else{
			//NOT THE HEAD
			while((*curr).next != NULL){
				if(strcmp( (*((*curr).next)).name, name) == 0){
					struct user* ret = (*curr).next;

					(*curr).next = (*((*curr).next)).next;		

					return ret;			
				}
				curr = (*curr).next;
			}
		}
	}

	return NULL;
}

struct user* removeUserListByFD(int fd){
	struct user* curr = head;

	if(curr == NULL){
		return NULL;
	}
	else{
		if((*curr).client_socket_fd == fd){
			//CHANGE THE HEAD TO THE NEXT
			//RETURN CURR
			head = (*curr).next;
			return curr;
		}
		else{
			//NOT THE HEAD
			while((*curr).next != NULL){
				if((*((*curr).next)).client_socket_fd == fd){
					struct user* ret = (*curr).next;

					(*curr).next = (*((*curr).next)).next;		

					return ret;			
				}
				curr = (*curr).next;
			}
		}
	}

	return NULL;
}

void clearUserList(){
	struct user* curr = head;

	if(curr == NULL){
		return; //ALREADY CLEARED
	}
	else{
		head = NULL;
		while(curr != NULL){
			int x = close((*curr).client_socket_fd);
			if(verbose){
				printf("IP Address: %s | Close/Shutdown Error: %i\n", (*curr).ip_address, x);
			}
			free(curr);

			curr = (*curr).next;
		}
	}
}

void printUserList(){

	struct user* curr = head;

	wprintf("\n+=| User List |=+\n");

	if(curr == NULL){
		wprintf("\nList is empty\n");
	}
	else{
		while(curr != NULL){
			printUser(curr);
			curr = (*curr).next;
		}
	}

	wprintf("\n+=----------=+\n");
}

void broadcastToUsers(char* msg){
	struct user* curr = head;

	while(curr != NULL){
		write(curr->client_socket_fd, msg, strlen(msg));

		curr = curr->next;
	}
}

//still need to close the FD
void disconnectUser(struct user* user){
	user->connected = 0;
	user->ip_address[0] = '\0';
	user->client_socket_fd = -1;
	user->thread_id = (pthread_t)-1;
	user->time_created = (time_t)-1;
}

void loginUser(struct user* dest, struct user* src){
	dest->connected = 1;
	strcpy(dest->ip_address, src->ip_address);
	dest->client_socket_fd = src->client_socket_fd;
	dest->thread_id = src->thread_id;
	dest->time_created = src->time_created;
}

int storePassword(struct user* user, char* password){
	if(password == NULL){
		return 0;
	}
	else if(validPassword(password)){
		memset(user->password, 0, MAX_INPUT);
		strcpy(user->password, password);
		generateSalt(user->salt);

		strcat(user->password, user->salt);
		if(hashPassword(user->password)){
			return 1;
		}
		else{
			return 0;
		}
	}
	else{
		return 0;
	}
}


void initDatabase(){
	//if table exists do simply open fd
	//else clear the file then open fd and create the table
   	int rc;
   	char *sql;
   	char* zErrMsg = 0;

   	/* Open database */
   	rc = sqlite3_open(accounts_file, &database);
   	if( rc ){
   		if(verbose){
      		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(database));
  		}
      	//create a new file with same name
      	int temp = open(accounts_file, O_RDWR|O_CREAT, 0644);
      	close(temp);
      	
   	}else{
   		if(verbose){
    		fprintf(stdout, "Opened database successfully\n");
   		}
   	}

   	sql = "create table users (name text primary key, password text, salt text);";
   	verbosePrint(sql);

	/* Execute SQL statement */
   	rc = sqlite3_exec(database, sql, NULL, 0, &zErrMsg);
   	if( rc != SQLITE_OK ){
   		if(verbose){
   			fprintf(stderr, "SQL error: %s\n", zErrMsg);
   		}
    	sqlite3_free(zErrMsg);
   	}else{
   		if(verbose){
    		fprintf(stdout, "Table created successfully\n");
		}
   	}

   	sqlite3_close(database);
}

void addUserToDatabase(struct user* user){
	//ADDS USER TO DATABASE

	//if table exists do simply open fd
	//else clear the file then open fd and create the table
   	int rc;
   	char sql[MAX_INPUT * 3];
   	memset(sql, 0, MAX_INPUT*3);
   	char* zErrMsg = 0;

   	/* Open database */
   	rc = sqlite3_open(accounts_file, &database);
   	if( rc ){
   		if(verbose){
      		fprintf(stderr, "Adding Users | Can't open database: %s\n", sqlite3_errmsg(database));      	
   		}
   	}else{
   		if(verbose){
    		fprintf(stdout, "Adding Users | Opened database successfully\n");
    	}
   	}

   	strcat(sql, "insert into users values(");

   	strcat(sql, "\'");
   	strcat(sql, user->name);
   	strcat(sql, "\'");

   	strcat(sql, ",");

   	strcat(sql, "\'");
   	strcat(sql, user->password);
    strcat(sql, "\'");

   	strcat(sql, ",");

   	strcat(sql, "\'");
   	strcat(sql, user->salt);
   	strcat(sql, "\'");

   	strcat(sql, ");");

   	verbosePrint("\n");
   	verbosePrint(sql);
   	verbosePrint("\n");

	/* Execute SQL statement */
   	rc = sqlite3_exec(database, sql, NULL, 0, &zErrMsg);
   	if( rc != SQLITE_OK ){
   		if(verbose){
   			fprintf(stderr, "Adding User | SQL error: %s\n", zErrMsg);
   		}
    	sqlite3_free(zErrMsg);
   	}else{
   		if(verbose){
    		fprintf(stdout, "Successfully added user\n");
		}
   	}

   	sqlite3_close(database);
}

static int retrieveUsers(void *NotUsed, int argc, char **argv, char **azColName){
   	int i;
   	char* currName;
   	char* currPass;
   	char* currSalt;

   	if(argc%3 != 0){
   		verbosePrint("Error in retrieve user. Amount of columns should be divisible by 3!");
   		return 0;
   	}

   	for(i=0; i<argc; i++){

       	//printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");

      	if(strcmp(azColName[i], "name") == 0){
      		currName = argv[i];
      	}
      	else if(strcmp(azColName[i], "password") == 0){
			currPass = argv[i];
      	}
      	else if(strcmp(azColName[i], "salt") == 0){
      		currSalt = argv[i];
      	}
      	else{
      		verbosePrint("Problem retrieving user, column does not match any created column");
      		return 0;
      	}

      	if(i%3 == 2){
      		struct user* newUser = createUser(
				0,
				NULL,
				currName, 
				-1,
				(pthread_t)-1,
				-1,
				currPass,
				currSalt
      		);

      		addUserList(newUser);
      		verbosePrint("Successfully adding ");
      		verbosePrint(currName);
      		verbosePrint(" to the User List");
      	}
   }
   verbosePrint("\n");
   return 0;
}

void retrieveUsersFromDatabase(){

	int rc;
   	char* sql;
   	char* zErrMsg = 0;

   	/* Open database */
   	rc = sqlite3_open(accounts_file, &database);
   	if( rc ){
   		if(verbose){
      		fprintf(stderr, "Retrieving Users | Can't open database: %s\n", sqlite3_errmsg(database));   
      	}   	
   	}else{
   		if(verbose){
    		fprintf(stdout, "Retrieving Users | Opened database successfully\n");
    	}
   	}

   	sql = "select * from users;";

	/* Execute SQL statement */
   	rc = sqlite3_exec(database, sql, retrieveUsers, 0, &zErrMsg);
   	if( rc != SQLITE_OK ){
   		if(verbose){
   			fprintf(stderr, "Retrieving User | SQL error: %s\n", zErrMsg);
   		}
    	sqlite3_free(zErrMsg);
   	}else{
   		if(verbose){
    		fprintf(stdout, "Retrieving User | Successfully retrieved users from database\n");
    	}
   	}

   	sqlite3_close(database);
}

/*End of List Functions*/

int main(int argc, char **argv){

	signal (SIGINT, catchCtrlC);

	//argv[1] = "4444";
	//argv[2] = "I am in CSE320";

	if(getCommands(argc, argv) == 0){
		return 0;
	}

	initDatabase();
	retrieveUsersFromDatabase();

	initSocket();

	printf("Currently listening on port %i.\n", port);

	selectLoop();

	close(socketfd);

}

void initSocket(){

	socketfd = socket(AF_INET, SOCK_STREAM, 0);

	if(socketfd < 0){
		error("socketfd has error");
	}

	(server_addr).sin_family = AF_INET;
	(server_addr).sin_port = htons(port);
	(server_addr).sin_addr.s_addr = INADDR_ANY;

	int yes = 1;

	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    	perror("setsockopt");
    	exit(1);
	}

	if(bind(socketfd, (struct sockaddr*)(&server_addr), sizeof(server_addr)) < 0){
		error("Problem binding server");
	}

	if (listen(socketfd, 5) == -1){
		error("Error in listen()");
	}

    if (fcntl(socketfd, F_SETFD, O_NONBLOCK) == -1){
    	error("Error in Unblock");
    }
}

int validateMessage(char** msg, int msgLen, char* verb){

	char* endBuffer = "\r\n\r\n";

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



int validPassword(char* pass){
	int hasFiveChars = strlen(pass) >= 5;
	int hasSymbolChar = 0;
	int hasNumberChar = 0; 
	int hasUpperCase = 0;
	int passwordGoodSize = strlen(pass) < MAX_INPUT;
	

	for(int i=0;i<strlen(pass);i++){
		char curr = pass[i];
		if( (curr >= 33 && curr <= 47) || 
			(curr >= 58 && curr <= 64) || 
			(curr >= 91 && curr <= 68) || 
			(curr >= 123 && curr <= 126) 
		){
			hasSymbolChar = 1;
		}
		else if( curr >= '0' && curr <= '9' ){
			hasNumberChar = 1;
		}
		else if( curr >= 'A' && curr <= 'Z' ){
			hasUpperCase = 1;
		}
	}

	return (hasFiveChars && hasSymbolChar && hasNumberChar && hasUpperCase && passwordGoodSize);
}

int correctPassword(struct user* user, char* password){
	if(strlen(password) > MAX_INPUT - strlen(user->salt)){
		return 0;
	}

	char hashedPassword[MAX_INPUT];
	memset(hashedPassword, 0, MAX_INPUT);

	strcat(hashedPassword, password);
	strcat(hashedPassword, user->salt);

	if(hashPassword(hashedPassword)){
		if(strcmp(user->password, hashedPassword) == 0){
			return 1;
		}
	}

	return 0;
}

void bye_protocol(int currfd){
	//bye
	char bye_send[MAX_INPUT];
	memset(bye_send, 0, MAX_INPUT);

	strcat(bye_send, BYE);
	strcat(bye_send, SPACE);
	strcat(bye_send, TERMINATION);

	write(currfd, bye_send, strlen(bye_send));

	verbosePrint(bye_send);
}

void err00_protocol(int currfd){
	//bad username
	write(currfd, ERR00, strlen(ERR00));
	verbosePrint(ERR00);
}

void err01_protocol(int currfd){
	//user not available
	write(currfd, ERR01, strlen(ERR01)); 
	verbosePrint(ERR01);

}

void err02_protocol(int currfd){
	//bad password
	write(currfd, ERR02, strlen(ERR02)); 
	verbosePrint(ERR02);
}

void err100_protocol(int currfd){
	//default
	write(currfd, ERR100, strlen(ERR100)); //sends default error
	verbosePrint(ERR100);
}

int loginProtocol(struct user* currUser){

	char msg_buffer[MAX_INPUT];
	memset(msg_buffer, 0, MAX_INPUT);

	int currfd = (*currUser).client_socket_fd;

		readInput(msg_buffer, currfd); 
		checkClientCrash(currUser, msg_buffer);
		verbosePrint(msg_buffer);

		int t_msgLen = 0;
		char **t_msg = tokenize(msg_buffer, &t_msgLen);

		if(strcmp(WOLFIE, t_msg[0]) == 0){ //WOLFIE end

			char eiflow_send[MAX_INPUT];
			memset(eiflow_send, 0, MAX_INPUT);

			strcat(eiflow_send, EIFLOW);
			strcat(eiflow_send, SPACE);
			strcat(eiflow_send, TERMINATION);

			write(currfd, eiflow_send, strlen(eiflow_send)); //EIFLOW end
			verbosePrint(eiflow_send);

		}

		free(t_msg);

		//IAM or IAMNEW
		memset(msg_buffer, 0, MAX_INPUT);
		readInput(msg_buffer, currfd); 
		checkClientCrash(currUser, msg_buffer);
		verbosePrint(msg_buffer);

		int msg_arrLen = 0;
		char** msg_arr = tokenize(msg_buffer, &msg_arrLen);

		if(validateMessage(msg_arr, msg_arrLen, IAMNEW) && msg_arrLen == 3){
			//send back HINEW
			char* currName = msg_arr[1];
			struct user* currUserInList = getUser(currName);

			if(currUserInList != NULL){
				//send username taken
				free(removeUserListByFD(currfd));
				err00_protocol(currfd);

				//bye
				bye_protocol(currfd);
			}
			else if(strlen(currName) > MAX_INPUT){
				free(removeUserListByFD(currfd));
				err100_protocol(currfd);
				bye_protocol(currfd);
			}
			else{
				//HINEW <name> \r\n\r\n
				char hinew_send[MAX_INPUT];
				memset(hinew_send, 0, MAX_INPUT);

				strcat(hinew_send, HINEW);
				strcat(hinew_send, SPACE);
				strcat(hinew_send, currName);
				strcat(hinew_send, SPACE);
				strcat(hinew_send, TERMINATION);
				verbosePrint(hinew_send);
				write(currfd, hinew_send, strlen(hinew_send));

				//read NEWPASS
				memset(msg_buffer, 0, MAX_INPUT);
				readInput(msg_buffer, currfd); 
				checkClientCrash(currUser, msg_buffer);
				verbosePrint(msg_buffer);

				free(msg_arr);
				int msg_arrLen = 0;
				msg_arr = tokenize(msg_buffer, &msg_arrLen);

				if(validateMessage(msg_arr, msg_arrLen, NEWPASS) && msg_arrLen == 3){
					//set new password
					char* password = msg_arr[1];

					if(storePassword(currUser, password)){
						//password is valid
						char ssapwen_send[MAX_INPUT];
						memset(ssapwen_send, 0, MAX_INPUT);

						strcat(ssapwen_send, SSAPWEN);
						strcat(ssapwen_send, SPACE);
						strcat(ssapwen_send, TERMINATION);

						write(currfd, ssapwen_send, strlen(ssapwen_send));
						verbosePrint(ssapwen_send);

						currUser->connected = 1;
						strcpy(currUser->name, currName);

						//addUserList(currUser);

						char hi_send[MAX_INPUT];
						memset(hi_send, 0, MAX_INPUT);

						strcat(hi_send, HI);
						strcat(hi_send, SPACE);
						strcat(hi_send, TERMINATION);

						write(currfd, hi_send, strlen(hi_send));
						verbosePrint(hi_send);

						//ADD USER INTO THE DATABASE!
						addUserToDatabase(currUser);

						char motd_send[MAX_INPUT];
						memset(motd_send, 0, MAX_INPUT);

						strcat(motd_send, MOTD);
						strcat(motd_send, SPACE);
						strcat(motd_send, motd);
						strcat(motd_send, SPACE);
						strcat(motd_send, TERMINATION);

						write(currfd, motd_send, strlen(motd_send));
						verbosePrint(motd_send);

						return 1;

					}
					else{

						free(removeUserListByFD(currfd));
						//bad password
						err02_protocol(currfd);

						//bye
						bye_protocol(currfd);
					}

				}
				else{
					free(removeUserListByFD(currfd));
					//default
					err100_protocol(currfd);

					//bye
					bye_protocol(currfd);
				}
			}

		}
		else if(validateMessage(msg_arr, msg_arrLen, IAM) && msg_arrLen == 3){
			char* currName = msg_arr[1];
			struct user* currUserInList = getUser(currName);

			if(currUserInList == NULL){ //account does not exist
				//reject connection
				//err01
				err01_protocol(currfd);
				bye_protocol(currfd);

				free(removeUserListByFD(currfd));
			}
			else if(strlen(currName) > MAX_INPUT){
				free(removeUserListByFD(currfd));
				err100_protocol(currfd);
				bye_protocol(currfd);
				
			}
			else{ //account exists

				if(currUserInList->connected == 1){ //user is already logged in
					//reject connection
					//err00
					err00_protocol(currfd);
					bye_protocol(currfd);

					free(removeUserListByFD(currfd));
				}
				else{ //user is not logged in so authenticate

					char auth_send[MAX_INPUT];
					memset(auth_send, 0, 1024);

					strcat(auth_send, AUTH);
					strcat(auth_send, SPACE);
					strcat(auth_send, currName);
					strcat(auth_send, SPACE);
					strcat(auth_send, TERMINATION);

					write(currfd, auth_send, strlen(auth_send));
					verbosePrint(auth_send);

					//read for PASS * \r\n\r\n
					memset(msg_buffer, 0, MAX_INPUT);
					readInput(msg_buffer, currfd); 
					checkClientCrash(currUser, msg_buffer);
					verbosePrint(msg_buffer);

					free(msg_arr);
					msg_arrLen = 0;
					msg_arr = tokenize(msg_buffer, &msg_arrLen);

					if(validateMessage(msg_arr, msg_arrLen, PASS) && msg_arrLen == 3){

						char* pass = msg_arr[1];
						free(msg_arr);

						if(correctPassword(currUserInList, pass)){ //correct password
							//send back ssap \r\n\r\n
							char ssap_send[MAX_INPUT];
							memset(ssap_send, 0, MAX_INPUT);

							strcat(ssap_send, SSAP);
							strcat(ssap_send, SPACE);
							strcat(ssap_send, TERMINATION);

							verbosePrint(ssap_send);
							write(currfd, ssap_send, strlen(ssap_send));

							//hi <name> \r\n\r\n
							char hi_send[MAX_INPUT];
							memset(hi_send, 0, MAX_INPUT);

							strcat(hi_send, HI);
							strcat(hi_send, SPACE);
							strcat(hi_send, currName);
							strcat(hi_send, SPACE);
							strcat(hi_send, TERMINATION);

							verbosePrint(hi_send);
							write(currfd, hi_send, strlen(hi_send));

							//log them in through currUserInList then remove this currUser
							removeUserListByFD(currfd);
							loginUser(currUserInList, currUser);

							//MOTD <motd> \r\n\r\n
							char motd_send[MAX_INPUT];
							memset(motd_send, 0, MAX_INPUT);

							strcat(motd_send, MOTD);
							strcat(motd_send, SPACE);
							strcat(motd_send, motd);
							strcat(motd_send, SPACE);
							strcat(motd_send, TERMINATION);

							write(currfd, motd_send, strlen(motd_send));
							verbosePrint(motd_send);

							return 1;
						}
						else{ //incorrect password
							//err02 bad password
							err02_protocol(currfd);
							bye_protocol(currfd);
						}

					}
					else{
						//wrong protocol message
						err100_protocol(currfd);
						bye_protocol(currfd);
					}

				}

			}

		}
		else{
			//wrong protocol message
			err100_protocol(currfd);
			bye_protocol(currfd);
		}

	removeUserListByFD(currfd);
	return 0;
}

void *loginThread(void *vargs){

	//signal(SIGPIPE, SIG_IGN);

	struct user* currConnecting = (struct user*)vargs;

	//int currfd = (*currConnecting).client_socket_fd;

	if(loginProtocol(currConnecting)){
		//wprintf("Finished login protocol with success\n");
		verbosePrint("Finished login protocol with success\n");

		currConnecting = getUserByFD(currConnecting->client_socket_fd);

		int cont = 1;

		while(cont){
			cont = handleClientCommands(currConnecting);
		}
	}
	else{
		verbosePrint("Failed login protocol");
	}

	return NULL;
}

int handleClientCommands(struct user* currConnecting){

	int currfd = (*currConnecting).client_socket_fd;

	char msg[MAX_INPUT];
	readInput(msg, currfd);

	checkClientCrash(currConnecting, msg);

	verbosePrint(msg);

	int msg_toklen = 0;
	char** msg_tok = tokenize(msg, &msg_toklen);

	if(strcmp(msg_tok[0], "TIME") == 0){
		time_t clientStartTime = (*currConnecting).time_created;
		time_t elapsedTime = getElapsedTime(clientStartTime);
		int et = (int)elapsedTime;
		

		char time[MAX_INPUT]; 
		memset(time, 0, MAX_INPUT);
		snprintf(time, 50, "%d", et);

		char time_send[MAX_INPUT];
		memset(time_send, 0, MAX_INPUT);

		strcat(time_send, EMIT);
		strcat(time_send, SPACE);
		strcat(time_send, time);
		strcat(time_send, SPACE);
		strcat(time_send, TERMINATION);

		write(currfd, time_send, strlen(time_send));

		verbosePrint(time_send);


		return 1;
	}
	else if(strcmp(msg_tok[0], "LISTU") == 0){
		struct user* curr = head;

		verbosePrint(UTSIL);
		verbosePrint(SPACE);

		write(currfd, UTSIL, strlen(UTSIL));
		write(currfd, SPACE, strlen(SPACE));


		while(curr != NULL){
			char *currName = (*curr).name;

			if(currName == NULL){
			}
			else{
				if(curr->connected == 1){
					write(currfd, currName, strlen(currName));
					write(currfd, " \r\n ", strlen(" \r\n "));

					verbosePrint(currName);
					verbosePrint(" \r\n ");
				}
			}

			curr = (*curr).next;
		}

		verbosePrint(TERMINATION);
		write(currfd, TERMINATION, strlen(TERMINATION));

		return 1;
	}
	else if(strcmp(msg_tok[0], "BYE") == 0){	
		bye_protocol(currfd);

		struct user* toRemove = getUserByFD(currfd); //removeUserListByFD(currfd);

		disconnectUser(toRemove);

		char uoff[MAX_INPUT];
		memset(uoff, 0, MAX_INPUT);

		strcat(uoff, "UOFF ");
		strcat(uoff, toRemove->name);
		strcat(uoff, " \r\n\r\n");

		broadcastToUsers(uoff);
		verbosePrint(uoff);

		close(currfd);
		return 0;
	}
	else if(strcmp(msg_tok[0], "MSG") == 0){

		verbosePrint(msg);

		char *to = msg_tok[1];
		char *from = msg_tok[2];

		struct user *to_user = getUser(to);
		struct user *from_user = getUser(from);

		if(to_user == NULL || from_user == NULL){
			err01_protocol(currfd);
		}
		else if(to_user->connected == 0 || from_user->connected == 0){
			err100_protocol(currfd);
		}
		else{
			int fd_to = (*to_user).client_socket_fd;
			int fd_from = (*from_user).client_socket_fd;

			write(fd_to, msg, strlen(msg));
			write(fd_from, msg, strlen(msg));

			verbosePrint(msg);
		}
	}

	return 1;
}


void selectLoop(){
	fd_set readfds;
	struct timeval timeout;

	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	char* cursor = cmd;

	while(1){
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		FD_SET(socketfd, &readfds);
		int sret = select(socketfd+1, &readfds, NULL, NULL, &timeout);

		for(int i=0;i<=socketfd && sret > 0; i++){
			if(FD_ISSET(i, &readfds)){
				if(i == socketfd){
					//ACCEPT THE CLIENT
					//ADD TO LIST

					struct sockaddr client_info;
					socklen_t client_info_len = sizeof(client_info);
					int client_socket = accept(socketfd, &client_info, &client_info_len);

					if(client_socket < 0){
						error("Error on accept() function");
					}

					//Convert ip here
					struct sockaddr_in *client_info_sai = (struct sockaddr_in *)(&client_info);
					char* ip_converted = inet_ntoa((*client_info_sai).sin_addr);

					struct user* currConnecting = createUser(
						0,
						ip_converted, //IP ADDRESS
						NULL, //NAME (none currently)
						client_socket, //CLIENT SOCKET 
						(pthread_t)-1, //threadID (none currently)
						time(NULL),
						NULL,
						NULL
					);

					addUserList(currConnecting);

					pthread_create(
						&((*currConnecting).thread_id),
						NULL, 
						loginThread, //callback function calling: void *loginThread
						(void *)currConnecting
					);

				}
				else if(i == 0){
					char last_char;
					int rv = read(0, &last_char, 1);
					if(!rv){
						error("Error in reading from stdin");
					}

					if(last_char == '\n'){
						*cursor = '\0';
						cursor = cmd;
						//execute command
						//write(1, cmd, strlen(cmd));
						//write(1, "\n", 1);
						handleCommand(cmd);
					}
					else{
						//append to current string
						*cursor = last_char;
						cursor++;
					}
					
				}
				else{
					//DO NOTHING
				}
			}
		}

	}

}

void handleCommand(char *cmd){
	if(cmd[0] != '\0'){
		int tc_len = 0;
		char** token_cmd = tokenize(cmd, &tc_len);
		//printArray(token_cmd, tc_len);

		if(strcmp(token_cmd[0], "/help") == 0){
			helpCommand();
		}
		else if(strcmp(token_cmd[0], "/users") == 0){
			printUserList();
		}
		else if(strcmp(token_cmd[0], "/shutdown") == 0){
			struct user* curr = head;

			while(curr != NULL){
				int currfd = (*curr).client_socket_fd;
				bye_protocol(currfd);

				curr = (*curr).next;
			}

			catchCtrlC();
		}
		else if(strcmp(token_cmd[0], "/accts") == 0){
			struct user* curr = head;

			wprintf("==+| Accounts |+==");
			wprintf("\n");

			while(curr != NULL){
				wprintf("\n");	
				wprintf("Name: ");
				wprintf(curr->name);
				wprintf("\n");
				wprintf("Password: ");
				wprintf(curr->password);
				wprintf("\n");
				wprintf("Salt: ");
				wprintf(curr->salt);
				wprintf("\n");	
				wprintf("\n");			

				curr = (*curr).next;
			}

			wprintf("==+----------+==");
			wprintf("\n");
		}

		free(token_cmd);
	}
}

void readInput(char cmd[MAX_INPUT], int fd){

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

    } 
    
    *cursor = '\0';

    // Execute the command, handling built-in commands separately 
    // Just echo the command line for now
    //write(1, cmd, strnlen(cmd, MAX_INPUT));
}


/*
void readInput(char cmd[MAX_INPUT], int fd){

	memset(cmd, 0, MAX_INPUT);

    char *cursor;
    char last_char;
    int rv = 1;
    int count;
      
    // read and parse the inpu
    for(rv = 1, count = 0, 
    cursor = cmd, last_char = 1;
    rv 
    && (++count < (MAX_INPUT-1))
    && (last_char != '\0');
    cursor++) { 

      rv = read(fd, cursor, 1);
      last_char = *cursor;
    } 
    
    *cursor = '\0';

    // Execute the command, handling built-in commands separately 
    // Just echo the command line for now
    //write(1, cmd, strnlen(cmd, MAX_INPUT));
}
*/


/*
void readInput(char cmd[MAX_INPUT], int fd){

	memset(cmd, 0, MAX_INPUT);

	fd_set readfds;
	struct timeval timeout;

	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	char* cursor = cmd;

	while(1){
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		int sret = select(fd+1, &readfds, NULL, NULL, &timeout);

		if(FD_ISSET(fd, &readfds) && sret > 0){
			char last_char;
			int rv = read(fd, &last_char, 1);
			if(!rv){
				error("Error in reading from stdin");
			}

			if(last_char == '\0'){
				*cursor = '\0';
				cursor = cmd;
				//execute command
				write(1, cmd, strlen(cmd));
				write(1, "\n", 1);
				handleCommand(cmd);
			}
			else{
				//append to current string
				*cursor = last_char;
				cursor++;
			}
				
		}
		else{
			*cursor = '\0';
			return;
		}

	}
}
*/


int getCommands(int argc, char** argv){
	//if return 0, then prompt help menu
	//otherwise do nothing

	if(argc < 3){
		printf("Too few arguments\n");
		printHelpMenu();
		return 0;
	}
	else if(argc > 5){
		printf("Too many arguments\n");
		printHelpMenu();
		return 0;
	}
	else{
		if(argc == 3){
			port = strtol(argv[1], NULL, 10);		
			if(port <= 0 || port > 65535){
				//invalid port
				printf("Port number must be between 0 and 65535");
				printHelpMenu();
				return 0;
			}
			motd = argv[2];
		}
		else if(argc == 4){
			if(strcmp(argv[1], "-h") == 0){
				printHelpMenu();
				return 0;
			}

			if(strcmp(argv[1], "-v") == 0){
				verbose = 1;
				port = strtol(argv[2], NULL, 10);		
				if(port <= 0 || port > 65535){
					//invalid port
					printf("Port number must be between 0 and 65535");
					printHelpMenu();
					return 0;
				}
				motd = argv[3];
			}
			else{ //means argv[3] is the accounts.db
				port = strtol(argv[1], NULL, 10);		
				if(port <= 0 || port > 65535){
					//invalid port
					printf("Port number must be between 0 and 65535");
					printHelpMenu();
					return 0;
				}
				motd = argv[2];
				accounts_file = argv[3];
			}

		}
		else if(argc == 5){
			if(strcmp(argv[1], "-h") == 0){
				printHelpMenu();
				return 0;
			}
			else{
				verbose = 1;
				port = strtol(argv[2], NULL, 10);		
				if(port <= 0 || port > 65535){
					//invalid port
					printf("Port number must be between 0 and 65535");
					printHelpMenu();
					return 0;
				}
				motd = argv[3];
				accounts_file = argv[4];
			}	

		}
		else{
			printf("Invalid amount of arguments.");
			printHelpMenu();
			return 0;
		}
	}

	if(accounts_file != NULL){
		char* file_ext = accounts_file + strlen(accounts_file) - 1 - 2;
		if(strcmp(file_ext, ".db") != 0){
			printf("ACCOUNTS_FILE MUST end with .db\n");
			printHelpMenu();
			return 0;
		}
	}
	else{
		accounts_file = "accounts_file.db";
	}

	/*
	if(argv[1] == NULL){
		printHelpMenu();
		return 0;
	}

	if(strcmp(argv[1], "-h") == 0){
		//help menu
		printHelpMenu();
		return 0;
	}
	else if(strcmp(argv[1], "-v") == 0){
		verbose = 1;

		if(argv[2] == NULL || argv[3] == NULL){
			printHelpMenu();
			return 0;
		}

		port = strtol(argv[2], NULL, 10);
		if(port <= 0 || port > 65535){
			//invalid port
			printHelpMenu();
			return 0;
		}
		motd = argv[3];
	}
	else{
		if(argv[1] == NULL || argv[2] == NULL){
			printHelpMenu();
			return 0;
		}

		port = strtol(argv[1], NULL, 10);		
		if(port <= 0 || port > 65535){
			//invalid port
			printHelpMenu();
			return 0;
		}
		motd = argv[2];
	}

	if(argv[5] != NULL){

	}
	else if(argv[4] != NULL){

	}
	*/

	//verbosePrint("Verbose: ");
	//verbosePrint(verbose);
	//verbosePrint("\n");

	if(verbose){
		printf("\nVerbose: %i\n", verbose);
	}

	//verbosePrint("Port Number: ");
	//verbosePrint(port);
	//verbosePrint("\n");

	if(verbose){
		printf("Port Number: %i\n", port);
	}

	verbosePrint("Message of the Day: ");
	verbosePrint(motd);
	verbosePrint("\n");

	if(accounts_file != NULL){
		verbosePrint("Accounts File located at: ");
		verbosePrint(accounts_file);
		verbosePrint("\n");
	}

	//printf("motd: %s\n", motd);

	return 1;

}

void printArray(char **array, int length){	
	for(int i=0;i<length;i++){
		if(array[i] == NULL){
			printf("%i | (null)\n", i);	
		}
		else{
			printf("%i | %s\n", i, array[i]);
		}
	}
}

char** alloc2DArray(int rows, int length){
	char **array = (char**)calloc(rows, sizeof(char*));

	for(int i=0;i<rows;i++){
		array[i] = (char*)calloc(length, sizeof(char));
	}

	return array;
}

char** tokenize(char *cmd, int* length){
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

void printHelpMenu(){
	printf("\n./server [-hv] PORT_NUMBER MOTD [ACCOUNTS_FILE]\n");
  	printf("-h \t\t\t displays help menu and returns EXIT_SUCCESS.\n");
  	printf("-v \t\t\t Verbose prints all incoming and outgoing protocol verbs and content.\n");
  	printf("PORT_NUMBER \t\t Port number to listen on.\n");
  	printf("MOTD \t\t\t Message to display when client connects.\n");
  	printf("ACCOUNTS_FILE \t\t File containing username and password to be loaded upon execution.\n");
}

void closeProperly(int signo){
	//will have signal handler to close process


	printf("Closed socket properly.");
}


void helpCommand(){
	char* one = "\n\n/users \t Prints all users connected to the server.\n";
	write(1, one, strlen(one));

	char* two = "/help \t Prompts this menu.\n";
	write(1, two, strlen(two));

	char* three = "/shutdown \t Shuts the server down.\n\n";
	write(1, three, strlen(three));
}

void shutdownCommand(){

}

void usersCommand(){

}

void wprintf(char* msg){
	write(1, msg, strlen(msg));
}

