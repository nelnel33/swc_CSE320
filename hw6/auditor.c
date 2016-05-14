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
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include "parser.h"

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )
#define MAX_INPUT 1024

char recv[MAX_INPUT];
char cmd[MAX_INPUT];

//FORWARD DECLARATIONS
void readInput(char cmd[MAX_INPUT]);
int getSocketFd(int argc, char** argv);
void handleMessage(char* cmd);
void selectLoop();

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

char* display_message = "Computerless Auditor -- Columns: | time | name | cmd | info |";
char info_message[MAX_INPUT] = "-h for help | -f <filter> [-c <column>] [-a | -d] |";
char* info_message_begin = "-h for help | -f <filter> [-c <column>] [-a | -d] |";
char curr_filter[MAX_INPUT];

int chat_cursor;
int chat_cursor_size;

typedef struct message{
	char msg[1024];

	struct message* next;
	struct message* prev;
}message;

struct message* message_head = NULL;

struct audit* scroll_pointer = NULL;

int audit_fd, audit_wd;
char* audit_file;

struct audit* curr_auditlog = NULL;

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
char** tokenize(char *cmd, int* length);


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
		//scroll_pointer = message_head;		
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

char* convertAuditToString(struct audit* a){

	if(a == NULL){
		return NULL;
	}
	if(a->time == NULL || a->name == NULL || a->cmd == NULL || a->info == NULL){
		return NULL;
	}

	char* string = calloc(1, MAX_INPUT);

	strcat(string, (a->time) );
	strcat(string, "\t | ");
	strcat(string, a->name);
	strcat(string, "\t | ");
	strcat(string, a->cmd);
	strcat(string, "\t | ");
	strcat(string, a->info);

	return string;
}

void printChatDisplay(struct audit* pointer){
	int lines = chat_display_end - chat_display_start + 1;

	char str[lines][MAX_INPUT];
	memset(str, 0, MAX_INPUT * lines);

	struct audit* temp = pointer;

	while(temp != NULL){

		char* string = convertAuditToString(temp);

		int arr_size = (strlen(string)/chat_display_size)+1;

		if(string != NULL){

			char **array = splitStringIntoArray(string, arr_size, chat_display_size);

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
		}

		free(string);

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
	chat_display_end = LINES-4;
	chat_display_size = COLS-5;

	text_field_start = LINES - 2;
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

	for(i=0;i<COLS-2;i++){
		mvaddch(2, i, ' ');
	}

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
	 	mvaddch(LINES-3, i, '-');
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

void showCurrentFilter(char* curr){
	strcpy(curr_filter, curr);

	memset(info_message, 0, MAX_INPUT);
	strcat(info_message, info_message_begin);
	strcat(info_message, " | Filter: ");
	strcat(info_message, curr_filter);

	initChatWindow();
}

void freeAuditList(struct audit* audit_head){
	struct audit* moveAudit = audit_head;
	while(moveAudit!=NULL)
	{
		struct audit* tempAudit = moveAudit;
		moveAudit = moveAudit->next;

		free(tempAudit);
	}
}

void filterPrintCmd(char* cmdFilter, char* cmdCol, int asc){

	fillAudit(audit_file);

	int callCol = -1;
	//printDebug("before cmdCol",9,1);

	if(cmdCol == NULL){
		callCol = -1;
	}
	else if(strcmp(cmdCol, "time") == 0){
		callCol = 0;
	}
	else if(strcmp(cmdCol, "name") == 0){
		callCol = 1;
	}
	else if(strcmp(cmdCol, "cmd") == 0){
		callCol = 2;
	}
	else if(strcmp(cmdCol, "info") == 0){
		callCol = 3;
	}
	//printDebug("after cmdCol", 9,1);

	if(cmdFilter == NULL && callCol == -1 && asc == -1){
		curr_auditlog = getAuditHead();;
	}
	else{

		if(cmdFilter != NULL && callCol != -1){
			char* temp = strstr(cmdFilter, "*");
			if(temp != NULL){
				*temp = '\0';
				temp++;
				curr_auditlog = filterByTimeRange(cmdFilter, temp);
				//printDebug("filterByTimeRange",9,1);
			}
			else{
				curr_auditlog = filterBy(callCol, cmdFilter);
				//printDebug("filterBy",9,1);
			}
		}
		else if(cmdFilter != NULL){
			curr_auditlog = searchByKeyword(cmdFilter);
			//printDebug("searchByKeyword",9,1);
		}
		else if(asc != -1 && callCol != -1){
			curr_auditlog = sortBy(callCol, asc);
		}
	}

	scroll_pointer = curr_auditlog;
	printChatDisplay(scroll_pointer);

				//printDebug("sortBy",5,1);
			//printDebug("!", 9, 9);
			//printDebugInt(asc, 9, 10);
			//printDebug("!", 9, 11);

}

void printHelpMenuInCurses(){
	clearChatDisplay();
	printDebug("Help Menu: -h", 4,1);
	printDebug("Available Columns: | time | name | cmd | info |",  5,1);
	printDebug("to sort by column (in asc(-a) or desc order(-d)): \t -c <column> -a|-d",6,1);
	printDebug("to filter by keyword: \t\t\t -f <keyword(s)>",7,1);
	printDebug("to filter a column by a keyword: \t\t -f <keyword(s)> -c <column>",8,1);
	printDebug("to filter by time: \t\t\t -f <time_begin>*<time_end> -c time", 9, 1);
	printDebug("to dump default audit log: \t\t\t -r", 10, 1);


}

void parseCmd(char *cmd){
	// [-f <filter>] [-c <column>] [-a | -d] 
	// if no exclamation point. Filter by keyword
	// - between <filter> denotes that it is from time to time.

	char cmdFilter[MAX_INPUT];
	char cmdCol[MAX_INPUT];

	memset(cmdFilter, 0, MAX_INPUT);
	memset(cmdCol, 0, MAX_INPUT);

	int asc = -1;

	int cmd_tok_len = 0;
	char **cmd_tok;
	if(cmd != NULL){
		cmd_tok = tokenize(cmd, &cmd_tok_len);

		if(strcmp(cmd_tok[1], "-h") == 0){
			printHelpMenuInCurses();
			return;
		}
		else if(strcmp(cmd_tok[1], "-r") == 0){
			filterPrintCmd(NULL, NULL, -1);
			return;
		}
	

		if(cmd_tok_len <= 0 || cmd_tok_len > 5){
			//too many commands
		}
		else{
			for(int i=0;i<cmd_tok_len;i++){
				if(strcmp(cmd_tok[i], "-f") == 0){
					strcpy(cmdFilter, cmd_tok[i+1]);
				}
				else if(strcmp(cmd_tok[i], "-c") == 0){
					strcpy(cmdCol, cmd_tok[i+1]);
				}
				else if(strcmp(cmd_tok[i], "-a") == 0){
					asc = 1;
					break;
				}
				else if(strcmp(cmd_tok[i], "-d") == 0){
					asc = 0;
					break;
				}
			}
		}

		if(cmdFilter[0] == '-'){
			memset(cmdFilter, 0, MAX_INPUT);
		}
		if(cmdCol[0] == '-'){
			memset(cmdCol, 0, MAX_INPUT);
		}

		filterPrintCmd(cmdFilter, cmdCol, asc);
	}
	else{
		filterPrintCmd(NULL,NULL,-1);
	}

	//printDebug(cmdFilter, 5,1);
	//printDebug(cmdCol, 6,1);
	//printDebugInt(asc, 7,1);
}

void handleMessage(char *cmd){
	clearChatDisplay();

	if(cmd[0] != '\0'){
		showCurrentFilter(cmd);
		parseCmd(cmd);
	}
	else{
		printChatDisplay(scroll_pointer);
	}

	//mvprintw(chat_display_end, 1, cmd);
}

void recvCommand(char *rcmd){
	addMessageList(createMessage(rcmd));
	clearChatDisplay();
	//printChatDisplay(message_head);
}


void selectLoop(){
	nbgetchar();

	int count = 0;

	fd_set readfds;
	struct timeval timeout;

	memset(&timeout, 0, sizeof(timeout));
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	audit_fd = inotify_init();

    if ( audit_fd < 0 ) {
        perror( "inotify_init" );
    }

    audit_wd = inotify_add_watch( audit_fd, audit_file, IN_MODIFY );

	while(1){

		if(ncursesReadInput()){
			//mvprintw(2,2,"good");
		}
		else{
			
		    FD_ZERO(&readfds);
		    FD_SET(audit_fd, &readfds);
		    int sret = select(audit_fd+1, &readfds, NULL, NULL, &timeout);

		    for(int i=0;i<=audit_fd && sret > 0; i++){
		      	if(FD_ISSET(i, &readfds)){

			        if(i == audit_fd){
			        	char buffer[BUF_LEN];
			        	memset(buffer, 0, BUF_LEN);
			          	int length = read( audit_fd, buffer, BUF_LEN );  

					    lseek(audit_fd, 0, SEEK_END);

					    if ( length < 0 ) {
					      	perror( "read" );
					    }  

					    //printf("%s has been modified %d times.\n", audit_file, count);

					    //display onto the window the structure pulled from the file with the current filter.
					    clearChatDisplay();
					    parseCmd(curr_filter);

					    printDebug("File modified", 7,1);
					    count++;
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
				clearChatDisplay();
				scrollUp();
				printChatDisplay(scroll_pointer);
			}
			else if(last_char == 'B'){
				//SCROLL DOWN IN CHATBOX
				setCursor(chat_cursor);
				clearTextField();
				printTextField(cmd);
				clearChatDisplay();
				scrollDown();
				printChatDisplay(scroll_pointer);
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

void printHelpMenu(){
	printf("./auditor audit.log\n");
	printf("audit.log \t Audit file you want to monitor.\n");
}

void getAuditFile(int argc, char **argv){
	if(argc < 2){
		printf("No audit file!"); 
		printHelpMenu();
		exit(0);
	}
	audit_file = argv[1];
}

int main(int argc, char** argv){

	signal(SIGWINCH, handle_winch);

	getAuditFile(argc, argv);

	initscr();

	cbreak();

	initChatWindow();

	filterPrintCmd(NULL, NULL, -1);

	setCursor(0);

	selectLoop();

	setCursor(chat_cursor);

	endwin();

}


