#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ncurses.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <pthread.h>


#define MAX_INPUT 1024

char cmd[MAX_INPUT]; //buffer for reading commands/messages
char recv[MAX_INPUT]; //buffer for recieving messages

int socketfd; //file descriptor this chat is speaking with

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

//FORWARD DECLARATIONS
void readInput(char cmd[MAX_INPUT]);
int getSocketFd(int argc, char** argv);
void handleMessage(char* cmd);
void selectLoop();

//HW6 Forward Declares
int getAuditFd(int argc, char** argv);
void writeCmdEvent(char* cmd, int success);

void error(char* message){
  fprintf(stderr,"%s\n", message);
  exit(0);
}

//COLS = # of cols
//LINES = # of rows
//mvprint(rows, columns, string);

int chat_display_start;
int chat_display_end;
int chat_display_size;

int text_field_start;
int text_field_end;
int text_field_size;

int auditFd;
char username[MAX_INPUT]; 
FILE* logFile;

char* display_message = "Computerless Chat";
char* info_message = "/close to exit chat | Up/Down to Scroll | Sent < | Received > |";

int chat_cursor;
int chat_cursor_size;

typedef struct message{
	char msg[1024];

	struct message* next;
	struct message* prev;
}message;

struct message* message_head = NULL;

struct message* scroll_pointer = NULL;

struct message* createMessage(char* msg);
void addMessageList(struct message* msg);
void printChatDisplay();
char** alloc2DArray(int rows, int length);
void printArray(char **array, int length);
char** splitStringIntoArray(char* str, int rows, int len);
void printTextField(char* cmd);
void insertCharIntoString(char *newString, char* string, char toInsert, int pos);
void removeCharFromString(char *newString, char* string, int pos);
void clearTextField();
void clearChatDisplay();
void initValues();
void printDebug(char *msg, int r, int c);
void printDebugInt(int i, int r, int c);
void getCursorYX(int pos, int *y, int *x);
void setCursor(int pos);
int moveCursor(int *cursor, int direction);
int nbgetchar();
void initChatWindow();
void handle_winch(int sig);
void handleMessage(char *cmd);
void selectLoop();
int ncursesReadInput();


struct message* createMessage(char* msg){
	struct message* temp = calloc(1, sizeof(*temp));

	strcpy(temp->msg, msg);
	temp->next = NULL;

	return temp;
}

void addMessageList(struct message* msg){
	if(message_head == NULL){
		message_head = msg;
	}
	else{
		msg->next = message_head;
		message_head->prev = msg;
		message_head = msg;
	}

	//if(scroll_pointer == NULL){
		scroll_pointer = message_head;		
	//}
}

void scrollUp(){

	if(scroll_pointer == NULL){

	}
	else{
		if(scroll_pointer->next == NULL){}
		else{
			scroll_pointer = scroll_pointer->next;
		}

		clearChatDisplay();
		printChatDisplay(scroll_pointer);


	}
}

void scrollDown(){

	if(scroll_pointer == NULL){

	}
	else{
		if(scroll_pointer->prev == NULL){}
		else{
			scroll_pointer = scroll_pointer->prev;

			clearChatDisplay();
			printChatDisplay(scroll_pointer);


		}
	}

}

void printChatDisplay(struct message* pointer){
	int lines = chat_display_end - chat_display_start + 1;

	char str[lines][MAX_INPUT];
	memset(str, 0, MAX_INPUT * lines);

	struct message* temp = pointer;

	while(temp != NULL){

		int arr_size = (strlen(temp->msg)/chat_display_size)+1;

		char **array = splitStringIntoArray(temp->msg, arr_size, chat_display_size);

		for(int i=arr_size-1;i>=0;i--){

			strcpy(str[lines-1], array[i]);

			--lines;
			if(lines <= 0){
				break;
			}
		}

		free(array);

		if(lines <= 0){
			break;
		}

		temp = temp->next;
	}

	int j=0;

	for(int i=0; i<(chat_display_end - chat_display_start + 1); i++){
		if(str[i] != NULL && str[i] != '\0'){
			mvprintw(chat_display_start+j, 1, str[i]);
			j++;
		}
	}

	//move cursor to correct position
	setCursor(chat_cursor);
}



char** alloc2DArray(int rows, int length){
	char **array = (char**)calloc(rows, sizeof(char*));

	for(int i=0;i<rows;i++){
		array[i] = (char*)calloc(length, sizeof(char));
	}

	return array;
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

char** splitStringIntoArray(char* str, int rows, int len){
	char **array = alloc2DArray(rows, len+1);

	for(int i=0;i<rows;i++){
		for(int j=0;j<len;j++){
			array[i][j] = str[(j%len) + i*len];
		}
	}

	return array;
}

void printTextField(char* cmd){

	//save cursor
	int y; int x;
	getyx(stdscr, y, x);

	char **array = splitStringIntoArray(cmd, (strlen(cmd)/text_field_size)+1, text_field_size);
	int pn = text_field_end - text_field_start + 1;

	//mvprintw(text_field_start, 1, array[0]);
	//mvprintw(text_field_start+1, 1, array[1]);

	for(int i=0;i<pn;i++){
		if(array[i] != NULL){
			mvprintw(text_field_start+i, 1, array[i]);
		}
	}

	free(array);

	move(y,x);
}

void insertCharIntoString(char *newString, char* string, char toInsert, int pos){
  //char* newString[sizeof(string)+1];
  //memset(newString, 0, sizeof(newString));

  if(pos < 0 || pos > strlen(string)){
    return;
  }

  for(int i=0;i<pos;i++){
    newString[i] = string[i];
  }

  newString[pos] = toInsert;

  for(int j=pos;j<strlen(string);j++){
    newString[j+1] = string[j];
  }

  newString[strlen(string)+1] = '\0';

  //return newString;
}

void removeCharFromString(char *newString, char* string, int pos){
  //char* newString[sizeof(string)-1];
  //memset(newString, 0, sizeof(newString));

  if(pos < 0 || pos >= strlen(string)){
    return;
  }

  for(int i=0;i<pos;i++){
    newString[i] = string[i];
  }

  for(int j=pos;j<strlen(string)-1;j++){
    newString[j] = string[j+1];
  }

  newString[strlen(string)-1] = '\0';

  //return newString;

}

void clearTextField(){
	//save cursor
	int y; int x;
	getyx(stdscr, y, x);

	for(int i=text_field_start; i<=text_field_end;i++){
		for(int j=1;j<=text_field_size;j++){
			mvaddch(i,j,' ');
		}
	}

	//restore cursor
	move(y,x);
}

void clearChatDisplay(){
	//save cursor
	int y; int x;
	getyx(stdscr, y,x);

	for(int i=chat_display_start; i<=chat_display_end;i++){
		for(int j=1;j<=chat_display_size;j++){
			mvaddch(i,j,' ');
		}
	}

	//restore cursor
	move(y,x);
}

void initValues(){
	chat_display_start = 4;
	chat_display_end = LINES-5;
	chat_display_size = COLS-5;

	text_field_start = LINES - 3;
	text_field_end = LINES - 2;
	text_field_size = COLS-2;

	chat_cursor= 0;
	chat_cursor_size = text_field_size * (text_field_end - text_field_start + 1);
}

void printDebug(char *msg, int r, int c){
	//save cursor
	int y; int x;
	getyx(stdscr, y,x);

	mvprintw(r, c,msg);

	//restore cursor
	move(y,x);
}

void printDebugInt(int i, int r, int c){
	char string[50];
	memset(string, 0, 50);
	snprintf(string, 50, "%d", i);
	printDebug(string, r, c);
}


void getCursorYX(int pos, int *y, int *x){
	int row = pos/text_field_size;
	int col = pos%text_field_size;

	*y = row;
	*x = col;
}

void setCursor(int pos){
	int row;
	int col;

	getCursorYX(pos, &row, &col);

	col++;

	move(text_field_start+row, col);
}


int moveCursor(int *cursor, int direction){
	// left == 1 | right == 2

	//printDebug("oldpos: ", 4, 1);
	//printDebugInt(*cursor ,5, 1);

	if(*cursor <= 0){
		if(direction == 2){
			(*cursor)++;
			return 1;
		}
	}
	else if((*cursor)+1 >= text_field_size * (text_field_end - text_field_start + 1)){
		if(direction == 1){
			(*cursor)--;
			return 1;
		}
	}
	else{
		if(direction == 1){
			(*cursor)--;
			return 1;
		}
		else if(direction == 2){
			(*cursor)++;
			return 1;
		}
	}

	//printDebug("Cursor moved: ", 6, 1);
	//printDebugInt(direction, 7, 1);
	//printDebug("newpos: ", 8, 1);
	//printDebugInt(*cursor, 9 ,1);
	return 0;

}

int nbgetchar(){
	nodelay(stdscr, TRUE);
	int c = getch();
	nodelay(stdscr, FALSE);

	return c;
}

void initChatWindow(){
	//save cursor position
	int x;
	int y;
	getyx(stdscr, y,x);

	int i;

	initValues();

	//init the chat boxes
	for(i=1;i<COLS-1;i++){
		mvaddch(0, i, '-');
	}
	for(int i=0;i<COLS;i++){
		mvaddch(chat_display_start-1, i, '-');
	}
	for(i=0;i<COLS;i++){
		mvaddch(LINES-1, i, '-');
	}
	for(i=0;i<COLS;i++){
	 	mvaddch(LINES-4, i, '-');
	}

	for(i=0;i<LINES;i++){
		mvaddch(i, 0, '|');
	}

	for(i=1;i<LINES-1;i++){
		mvaddch(i, COLS-1, '|');
	}

	mvaddch(0,0,'+');
	mvaddch(0,COLS-1,'+');
	mvaddch(LINES-1,0,'+');
	mvaddch(LINES-1,COLS-1,'+');

	start_color();
	init_pair(1, COLOR_BLUE, COLOR_BLACK);
	attron(COLOR_PAIR(1));
 	//init the display text
	if(strlen(display_message) <= COLS - 2){
		int start = (COLS - 2 - strlen(display_message))/2;

		mvprintw(1 ,start, display_message);
	}
	else{
		for(i=1;i<COLS-2;i++){
			mvaddch(1, i, display_message[i]);
		}
	}


	//init the display text
	if(strlen(info_message) <= COLS - 2){
		int start = (COLS - 2 - strlen(info_message))/2;

		mvprintw(2 ,start, info_message);
	}
	else{
		for(i=1;i<COLS-2;i++){
			mvaddch(2, i, info_message[i]);
		}
	}
	attroff(COLOR_PAIR(1));

	//init the scroll bar
	for(i=chat_display_start; i<=chat_display_end; i++){
		mvaddch(i, COLS-4, '|');
	}
	//mvaddch(chat_display_start+1, COLS-3, '-');
	//mvaddch(chat_display_end-1, COLS-3, '-');
	mvaddch(chat_display_start, COLS-3, '^');
	mvaddch(chat_display_end, COLS-3, 'v');

	wmove(stdscr, y,x);

	//restore cursor position
}

void handle_winch(int sig){
	endwin(); 
	initscr();
	refresh();
	clear();
	initChatWindow();
	refresh();
}

/*
void handleMessage(char *cmd){
	mvprintw(chat_display_start, 1, cmd);
}
*/


int getSocketFd(int argc, char** argv){
  if(argc < 2){
    printf("Invalid file descriptor\n");
    exit(0);
  }
  else{
    return strtol(argv[1], NULL, 10);
  }
}

int getAuditFd(int argc, char** argv)
{
	return strtol(argv[2], NULL, 10);
}

void getUsername(int argc, char** argv)
{
	strcpy(username, argv[3]);
}


void handleMessage(char* cmd)
{
    if(strcmp(cmd, "/close")==0)
  {
  	//write(socketfd, "CLOSING CHAT WINDOW \r\n\r\n", strlen("CLOSING CHAT WINDOW \r\n\r\n"));
  	logFile = fdopen(auditFd, "a+");
  	writeCmdEvent("/close" ,1);
    exit(EXIT_SUCCESS);
  }
  else
  {
    write(socketfd, cmd, strlen(cmd));
    write(socketfd, " \r\n\r\n", strlen(" \r\n\r\n"));
  }
}

void recvCommand(char *rcmd){
	addMessageList(createMessage(rcmd));
	clearChatDisplay();
	printChatDisplay(message_head);
}


void selectLoop(){
	nbgetchar();

	fd_set readfds;
	struct timeval timeout;

	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	//char* cursor = cmd;
	char* rcursor = recv;

	//int i=0;

	while(1){

		//printDebugInt(i,1,1);
		//i++;
		if(ncursesReadInput()){
			//mvprintw(2,2,"good");
		}
		else{

		    FD_ZERO(&readfds);
		    FD_SET(socketfd, &readfds);
		    int sret = select(socketfd+1, &readfds, NULL, NULL, &timeout);

		    for(int i=0;i<=socketfd && sret > 0; i++){
		      	if(FD_ISSET(i, &readfds)){

			        if(i == socketfd){
			          char last_char;
			          int rv = read(socketfd, &last_char, 1);


			          if(!rv){
			            error("Error in reading from socketfd");
			          }

			          if(last_char == '\n'){
			            *rcursor = '\0';
			            rcursor = recv;

			            recvCommand(recv);
			          }
			          else{
			            //append to current string
			            *rcursor = last_char;
			            rcursor++;
			          }
			        }
			        else{

			       	}
		    	}
		    }
		}

	}
}

int ncursesReadInput(){
    char last_char = nbgetchar();

	if(last_char == 27){
		last_char = nbgetchar();
		if(last_char == '['){
			last_char = nbgetchar();
			if(last_char == 'A'){
				//SCROLL UP IN CHATBOX
				setCursor(chat_cursor);
				clearTextField();
				printTextField(cmd);
				scrollUp();
			}
			else if(last_char == 'B'){
				//SCROLL DOWN IN CHATBOX
				setCursor(chat_cursor);
				clearTextField();
				printTextField(cmd);
				scrollDown();
			}
			else if(last_char == 'D'){
				moveCursor(&chat_cursor, 1);//move left
				setCursor(chat_cursor);
				clearTextField();
				printTextField(cmd);
			}
			else if(last_char == 'C'){
				moveCursor(&chat_cursor, 2);//move right
				setCursor(chat_cursor);
				clearTextField();
				printTextField(cmd);
			}
			else{
				//DO NOTHING
			}

		}
		else if(last_char == 'i'){

		}
		else{

		}
	}
	
	else if(last_char == 10){
    	clearTextField();
    	handleMessage(cmd);
    	chat_cursor = 0;
    	setCursor(chat_cursor);
    	memset(cmd, 0, MAX_INPUT);

	}
	else if(last_char == 13){

	}
	else if(last_char == -1){
		return 0;
	}
	else{
		if(last_char == 127){
			moveCursor(&chat_cursor, 1);
			setCursor(chat_cursor);

			clearTextField();

			char tempbuff[MAX_INPUT];
			memset(tempbuff, 0, MAX_INPUT);

			removeCharFromString(tempbuff, cmd, chat_cursor);

			strcpy(cmd, tempbuff);

			printTextField(cmd);
		}
		else{
			char tempbuff[MAX_INPUT];
			memset(tempbuff, 0, MAX_INPUT);

			if(chat_cursor+1 >= text_field_size * (text_field_end - text_field_start + 1)){
				//DO NOTHING
			}
			else{	
				insertCharIntoString(tempbuff, cmd, last_char, chat_cursor);
				strcpy(cmd, tempbuff);
			}

			moveCursor(&chat_cursor, 2);
			setCursor(chat_cursor);

			clearTextField();

			//printTextField(cmd);
			//printDebug(cmd, text_field_start, 1);
			printTextField(cmd);

		}	
	}

	return 1;
}



/*
void selectLoop(){
  	while(1){
    	char last_char = nbgetchar();

    	if(last_char == 27){
    		last_char = nbgetchar();
    		if(last_char == '['){
    			last_char = nbgetchar();
    			if(last_char == 'A'){
    				//SCROLL UP IN CHATBOX
    				clearTextField();
    				printTextField(cmd);
    			}
    			else if(last_char == 'B'){
    				//SCROLL DOWN IN CHATBOX
    				clearTextField();
    				printTextField(cmd);
    			}
    			else if(last_char == 'D'){
    				moveCursor(&chat_cursor, 1);//move left
    				setCursor(chat_cursor);
    				clearTextField();
    				printTextField(cmd);
    			}
    			else if(last_char == 'C'){
    				moveCursor(&chat_cursor, 2);//move right
    				setCursor(chat_cursor);
    				clearTextField();
    				printTextField(cmd);
    			}
    			else{
    				//DO NOTHING
    			}

    		}
    		else if(last_char == 'i'){

    		}
    		else{

    		}
    	}
    	
    	else if(last_char == 10){
        	clearTextField();
        	handleMessage(cmd);
        	chat_cursor = 0;
        	memset(cmd, 0, MAX_INPUT);
    	}
    	else if(last_char == 13){

    	}
    	else if(last_char == -1){
    		//DO NOTHING
    	}
    	else{
    		if(last_char == 127){
    			moveCursor(&chat_cursor, 1);
    			setCursor(chat_cursor);

    			clearTextField();

    			char tempbuff[MAX_INPUT];
    			memset(tempbuff, 0, MAX_INPUT);

    			removeCharFromString(tempbuff, cmd, chat_cursor);

    			strcpy(cmd, tempbuff);

    			printTextField(cmd);
    		}
    		else{
    			char tempbuff[MAX_INPUT];
    			memset(tempbuff, 0, MAX_INPUT);

    			if(chat_cursor+1 >= text_field_size * (text_field_end - text_field_start + 1)){
    				//DO NOTHING
    			}
    			else{	
    				insertCharIntoString(tempbuff, cmd, last_char, chat_cursor);
    				strcpy(cmd, tempbuff);
    			}

    			moveCursor(&chat_cursor, 2);
    			setCursor(chat_cursor);

				clearTextField();
  
    			//printTextField(cmd);
    			//printDebug(cmd, text_field_start, 1);
    			printTextField(cmd);

    		}	
    	}
	}
}*/

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

void sfwriteTest(pthread_mutex_t *lock, FILE* stream, char *fmt, ...){
	pthread_mutex_lock(lock);

	va_list ap;

	va_start(ap, fmt);
	vfprintf(stream, fmt, ap);
	va_end(ap);
	fflush(stream);

	pthread_mutex_unlock(lock);
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
	sfwriteTest(&mutex, logFile, "%s", outstr);

}

void writeLogEvent(char* event)
{
	writeTimestamp();

	sfwriteTest(&mutex, logFile, ", %s", username);
	sfwriteTest(&mutex, logFile, ", %s\n", event);
}



void writeCmdEvent(char* cmd, int success)
{
	char* messageStr = calloc(MAX_INPUT, sizeof(char*));

	strcat(messageStr, "CMD, ");

	trimNewLine(cmd);
	strcat(messageStr, cmd);
	strcat(messageStr, ", ");

	if(success==0)
		strcat(messageStr, "fail, ");
	else strcat(messageStr, "success, ");
	strcat(messageStr, "chat");

	writeLogEvent(messageStr);

	free(messageStr);
}

int main(int argc, char** argv){

	socketfd = getSocketFd(argc, argv);

	getUsername(argc, argv);
	auditFd = getAuditFd(argc, argv);



	signal(SIGWINCH, handle_winch);

	//chat_display_start = 10;
	//chat_display_end = 15;
	//chat_display_size = 80;
	if (fcntl(socketfd, F_SETFD, O_NONBLOCK) == -1){
    	error("Error in Unblock");
    }

	initscr();

	cbreak();

	initChatWindow();

	setCursor(chat_cursor);

	nbgetchar();

	selectLoop();

	endwin();

}


