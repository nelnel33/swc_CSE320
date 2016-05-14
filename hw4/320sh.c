#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

// Assume no input line will be longer than 1024 bytes
#define MAX_INPUT 1024
#define MAX_PATH_LENGTH 1000
#define MAX_ARGV_LENGTH 1000
#define MAX_ARGV_ROWS 100

#define ANSI_UP "\033[1A"
#define ANSI_DOWN "\033[1B"
#define ANSI_LEFT "\033[1D"
#define ANSI_RIGHT "\033[1C"

typedef void (*handler_t)(int);

//Process struct

struct process
{
  int full;
  char** argv;
  int argvLength;
  int input_fd;
  int output_fd;
  int inputFd;
  int outputFd;

  int pid;

  char* redirectInput;
  char* redirectOutput;

  struct process *nextProcess;

};

struct job
{
  struct process *firstProcess;
  pid_t pgid;
  int jpid;

  int bg;

  struct job* nextJob;

};

//Forward declarations
void getPaths(char **envp);
void setBuiltInCommand(char* cmd);
void executeBuiltInCommand(char* cmd, char** envp);
void callProgram(char* cmd, struct process* exeProcess, char** argv, char** envp, int fileIn, int fileOut, int pgid, int bg);
void trimNewLine(char* cmd);
int printPrompt();
int containsSlash(char* cmd);
char* getFirstCommand(char* cmd, char* cmdCmp);
void getArgv(char* cmd, char** newArgv, int* length);
void printStringArray(char** arr, int length);
void printHelpMenu();
void executeCD(char* cmd, char** envp);
int executeSet(char* cmd, char** envp);
void executeEcho(char* cmd);

//USED FOR PRINTING OUT TO CONSOLE
void clearPrompt(int amount);
void whiteOutPrompt(int amount);
void cleanPrompt(int amount);//contains clearPrompt then whiteout then clear again
void resetPrompt();
int getPromptSize();

int getTerminalColumns();
int getRowsToBacktrack(char *cmd);
void backTrackRows(int rows);

//USED TO CALL HISTORY ARRAY
int storeHistory(char* hist);
int loadHistory(char *buffer, int direction);
int openHistoryFile();
int storeHistoryToFile();
int getHistoryFromFile();
int lengthNewLine(char *string);
//ONLY USED TO PRINT THE HISTORY ARRAY!
void printNewLineArray(char arr[50][MAX_INPUT]);

void printHistory();
void clearHistoryFile();

//QUOTE PARSING
int numberOfQuotes(char* string);
void parseArgsFromQuotes(char* string, char** storeHere, int *length);
void combineArrayIntoString(char *buffer, char** array, int size);
char** alloc2DArray(int rows, int length);
void removeCharFromStringWithoutIndex(char* string, char garbage);

//Used for piping and redirection
void parseIntoProcesses(char* cmd, struct job* newJob);
void strArrayCpy(char** destStrArr, char** cpyStrArr, int cpyLength);
void printProcess(struct process* printProcess);
void calloc2dArray(char** arr, int rows, int length);
void printJob(struct job* printJob);
void executeJob(struct job* newJob, char** envp);
void reconstructCmd(char* cmd, char** argv, int argvLength);
int containsOutputRedirect(char* arg);
int containsInputRedirect(char* arg);

//Used for job handling
void controlC();
void controlZ();
int parseForAnpersand(char** argv, int length);
void addJobToList(struct job* newJob);
void printJobList();
void printBackgroundJobList();
void scanForBackgroundCharacter(struct job* newJob);
void assignJid(struct job* newJob);
void fg(char* cmd);
void checkAndRemoveFinalProcess(struct job* job, struct process* process);

//Debugging
void setDebugStatus(char** argv, int length);

/**
* Inserts one character into the string at a certain position
* char* string is the string that the character will be inserted into
* char toInsert is the char to be inserted into the string
**/
void insertCharIntoString(char* newString, char* string, char toInsert, int pos);

/**
* Removes one character into the string at a certain position
* char* string is the string that the character will be removed from
**/
void removeCharFromString(char* newString, char* string, int posToRem);

/**
* Gets the substring from begin(inclusive) to end(exclusive)
* char* string is the base string
* int begin is the substrings beginning index
* int end is the substrings end index
* returns the substring from begin to end
**/
int substring(char *bigString, char *subdest, int begin, int end);

void tokenize(char *string, char** args, int* length);



//These really shouldn't be 100
char *paths[MAX_PATH_LENGTH];
int pathSize = 0;
int builtInCommand = 0;
char *prompt = "320sh> ";
char lastDir[MAX_PATH_LENGTH];

//HISTORY
char history[50][MAX_INPUT];
int currHistory = 0; //index to where the current is in the history
int currHistoryOffset = 0; //which line of type to reference relative where to write.
int history_fd;

//Debug
int debug = 0;

struct job *jobHead = NULL;

int 
main (int argc, char ** argv, char **envp) {

  //pause();
  //return 0;

  //printf("%c[%dA", 0x1B, 5);

  getHistoryFromFile();

  //Set debug status

  setDebugStatus(argv, argc);


  int finished = 0;
  char cmd[MAX_INPUT];
  memset(cmd,0,MAX_INPUT);//clears the cmd

  char cmdQuoteBuffer[MAX_INPUT];
  memset(cmdQuoteBuffer, 0, MAX_INPUT);


  //Get path environment variables. Put them in paths
  getPaths(envp);
  /*for(int i = 0; i <=pathSize; i++)

    {
      printf("Paths: %s\n", paths[i]);
    }*/



  while (!finished) {
    char *cursor;
    char last_char;
    int rv;
    int count;
    char currChar[1];



    // Print the prompt
    //printf("Prompt works?\n");
    //rv = write(1, prompt, strlen(prompt));
    rv = printPrompt();
    if (!rv) { 
      finished = 1;
      break;
    }
    
    // read and parse the input
    for(rv = 1, count = 0, 
	  cursor = cmd, last_char = 1;
	  rv 
	  && (++count < (MAX_INPUT-1))
	  && (last_char != '\n');
	cursor++) { 
      //printf("count: %i\n", count);
      rv = read(0, currChar, 1);
      last_char = *currChar;


      if(last_char == 27){//Checks if it is an esc character ^[
        read(0,currChar,1);
        last_char = *currChar;
        if(last_char == '['){//Checks if next character is [
          read(0,currChar,1);
          last_char = *currChar;
          if(last_char == 'A'){//checks if up
            --count; //keeps the cursor in place
            cleanPrompt(strlen(cmd));
            loadHistory(cmd, 0);
            resetPrompt();
            count = strlen(cmd);
            cursor = cmd + count;
            //printf("up");
            //printNewLineArray(history);
          }
          else if(last_char == 'B'){//down
            --count; //keeps the cursor in place
            cleanPrompt(strlen(cmd));
            loadHistory(cmd, 1);
            resetPrompt();
            count = strlen(cmd);
            cursor = cmd + strlen(cmd);
            //printf("down");
            //printNewLineArray(history);
          }
          else if(last_char == 'D'){//left
            if(count > 1){
              write(1,"\b",1);
              --count;//shifts the left one
            }
            --count;//otherwise only keeps it in place


          }
          else if(last_char == 'C'){//or right
            //*(cursor) = 0;
            if(count >= strlen(cmd)){
              --count;
            }
            else{
              write(1, (cmd+count-1), 1);
            }
            //printf("count: %i\n", count);
          }         
        }
      }
      else if(last_char == 3) 
      {
        controlC();
      }
      else if(last_char == 26)
      {
        controlZ();
      }
      else if(last_char == 127){//backspace, currently only deletes from the beginning.
        if(count == 1){
          whiteOutPrompt(1);
          clearPrompt(1);
          count--;          
        }
        if(count >=2){
          char tempbuff[sizeof(cmd)];
          memset(tempbuff,0,sizeof(cmd));

          cleanPrompt(getPromptSize()+strlen(cmd));
          printPrompt();

          removeCharFromString(tempbuff, cmd, count-2);
          count-=2;
          //printf("\ncount: %i\n", count);
          //printf("\ntempbuff: %s\n", tempbuff);
          strcpy(cmd, tempbuff);
          write(1,cmd,strlen(cmd));

          whiteOutPrompt(count);
          clearPrompt(count);
          int backtrack = (strlen(cmd)) - count;
          clearPrompt(backtrack);
        }
        //printf("cmd: %s\n", cmd);
        //printf("count: %i\n", count);
      }
      else if(last_char == '\n'){}
      else {
	      //write(1, &last_char, 1);
        //*cursor = currChar[0];
        char tempbuff[sizeof(cmd)];
        memset(tempbuff,0,sizeof(cmd));

        cleanPrompt(getPromptSize() + strlen(cmd));
        //backTrackRows(getRowsToBacktrack(cmd));
        printPrompt();
        //printf("\nprompt length: %i\n", getPromptSize());

        //printf("\ncount: %i\n", count);
        //printf("\ncmd: %s\n", cmd);

        insertCharIntoString(tempbuff, cmd, currChar[0], count-1);
        //printf("\ncount: %i\n", count);
        //printf("\ntempbuff: %s\n", tempbuff);
        strcpy(cmd, tempbuff);
        write(1,cmd,strlen(cmd));

        //int backtrack = (strlen(cmd)) - count;
        //clearPrompt(backtrack);
      }
    }

    write(1,"\n", 1);

    strcat(cmdQuoteBuffer, cmd);

    if(numberOfQuotes(cmdQuoteBuffer) % 2 == 0 && cmd[0] != '\0'){ 
      //IF IT HAS AN EVEN NUMBER OF QUOTES IT WILL EXECUTE WHATEVER
      //IF ODD NUMBER OF QUOTES MEANS THERE MUST BE AN OPEN QUOTE
        //SO IT CONTINUES



      if(numberOfQuotes(cmdQuoteBuffer) > 0){
        strcpy(cmd, cmdQuoteBuffer); 
      }

        //printf("numberOFQuotes cmd: %s", cmd);

      //WE STILL NEED TO RUN IT THROUGH THE QUOTE ARGUMENT PARSER!

        storeHistory(cmd);

        *cursor = '\0';

      if (!rv) { 
        finished = 1;
        break;
      }


      // Execute the command, handling built-in commands separately 
      // Just echo the command line for now
      //write(1, cmd, strnlen(cmd, MAX_INPUT));

      //printf("\ncmd before job: %s\n", cmd);

      struct job* newJob = calloc(sizeof(struct job), 1);
      //Doing testing for pipes
      parseIntoProcesses(cmd, newJob);

      addJobToList(newJob);
      printJobList();
      //printBackgroundJobList();

      executeJob(newJob, envp);
      
      /*Taken out for piping testing
      //Check if it's built in
      setBuiltInCommand(cmd);
      //If it is, execute it as a built in function
      if(builtInCommand==1)
        executeBuiltInCommand(cmd, envp);
      //Otherwise, try to call it as a program
      else callProgram(cmd, argv, envp);

      */

      memset(cmdQuoteBuffer, 0, sizeof(cmdQuoteBuffer)); 
      //RESETS THE QUOTE BUFFER only if it executes a command
    }
    else{

    }

    memset(cmd, 0, sizeof(cmd)); //resets the cmd string
    
  }

  storeHistoryToFile();

  return 0;
}

void printHistory(){
  printf("\n--History Dump--\n");
  for(int i=0;i<currHistory;i++){
    if(history[i][0] != '\0'){
      printf("%s\n", history[i]);
    }
  }
}

int getTerminalColumns(){
  char* col = "COLUMNS";
  char* colenv = getenv(col);
  if(colenv != NULL){
    //printf("colenv: %s",colenv);
  }
  else{
    //printf("colenv = null");
  }
  //long lcol = strtol(colenv, (char**)NULL, 10);
  return 80;//(int)lcol;
}

int getRowsToBacktrack(char *cmd){
  int totalSize = getPromptSize() + strlen(cmd);
  int tc = getTerminalColumns();
  int rtbc = totalSize/tc;

  //printf("\nTerminal Columns: %i\n", tc);
  //printf("\nPromptSize+cmd: %i\n", totalSize);
  //printf("\nRows to backtrack: %i\n", rtbc);

  return rtbc;
}

void backTrackRows(int rows){
  for(int i=0;i<rows+1;i++){
    write(1,"\r",1);
    //int t = getTerminalColumns();
    //whiteOutPrompt(t);
    printf("\r");
    //printf("%s", ANSI_UP);
    //printf("%c[%dA", 0x1B, 5);
  }
}

void parseArgsFromQuotes(char* string, char** storeHere, int *length){
  tokenize(string, storeHere, length);
  int modLength = *length;  

  int begin = 0;
  int quoteArgCount = 0;

  for(int i=0;i<*length;i++){
    if(numberOfQuotes(storeHere[i]) % 2 != 0){
      if(!begin){
        begin = 1;
        quoteArgCount++;
      }
      else{
        begin = 0;
        combineArrayIntoString(storeHere[i-quoteArgCount+1], &storeHere[i-quoteArgCount+1], quoteArgCount);
        modLength-=(quoteArgCount-1);
        quoteArgCount = 0;
      }
    }
    if(begin){
      quoteArgCount++;
    }
  }

  for(int z=*length;z>=0;z--){
    if(storeHere[z][0] == '\0'){
      //shift everything down
      for(int j=z;j<(*length)-1;j++){
        strcpy(storeHere[j], storeHere[j+1]);
      }
    }
  }

  *length = modLength;

  for(int b=0;b<*length;b++){
    removeCharFromStringWithoutIndex(storeHere[b], '\"');
  }
}

void removeCharFromStringWithoutIndex(char* string, char garbage){
  for(int i=(strlen(string)-1); i>=0; i--){
    if(string[i] == garbage){
      for(int j=i;j<strlen(string);j++){
        string[j] = string[j+1];
      }
    }
  }
}

void combineArrayIntoString(char* buffer, char** array, int size){

  strcpy(buffer, (array[0]));

  for(int i=1;i<size;i++){
    strcat(buffer, (array[i]));
    (array[i])[0] = '\0';// clears 
  }

}

int numberOfQuotes(char* string){
  int count = 0;
  for(int i=0;i<strlen(string);i++){
    if(string[i] == '\"')
      count++;
  }

  return count;
}

void printNewLineArray(char arr[50][MAX_INPUT]){
  char temp[MAX_INPUT];
  for(int i=0;i<50;i++){
    strcpy(temp, arr[i]);
    trimNewLine(temp);
    printf("\n| %i : %s |\n",i, temp);
  }
}

void clearPrompt(int amount){
  for(int i=0;i<amount;i++){
    write(1,"\b", 1);
  }
}
void whiteOutPrompt(int amount){
  for(int i=0;i<amount;i++){
    write(1," ", 1);
  }
}

void cleanPrompt(int amount){
  clearPrompt(amount);
  whiteOutPrompt(amount);
  clearPrompt(amount);
}

void resetPrompt(char* cmd){
    cleanPrompt(getPromptSize());
    printPrompt();
    write(1,cmd,strlen(cmd));

    //int backtrack = (strlen(cmd)) - count;
    //clearPrompt(backtrack);
}

int lengthNewLine(char* string){
  int i=0;
  while(*(string+i) != '\n'){
    i++;
  }

  return i;
}

int openHistoryFile(){
  char *homeStr = "HOME";
  char *historyFile;
  historyFile = getenv(homeStr);

  strcat(historyFile, "/.320sh_history");

  //history_fd = open(historyFile, O_RDWR|O_CREAT, 0644);
  history_fd = open("CSE320.txt", O_RDWR|O_CREAT, 0644);

  return history_fd;
}

void clearHistoryFile(){
  //char *homeStr = "HOME";
  //char *historyFile;
  //historyFile = getenv(homeStr);

  unlink("CSE320.txt");
  history_fd = open("CSE320.txt", O_RDWR|O_CREAT, 0644);
  close(history_fd);

  for(int i=0;i<50;i++){
    memset(history[i], 0, MAX_INPUT);
  }

  printf("History cleared\n");
}

int storeHistoryToFile(){
  openHistoryFile();

  if(history_fd < 0){
    return 0;
  }

  char *homeStr = "HOME";
  char *historyFile;
  historyFile = getenv(homeStr);

  strcat(historyFile, "/.320sh_history");

  unlink(historyFile);

  for(int i=0;i<50;i++){
    if(history[i][0] != '\0'){
      write(history_fd, history[i], strlen(history[i]));
      write(history_fd, "\n", 1);
    }
  }

  close(history_fd);
  return 1;
}

int getHistoryFromFile(){
  openHistoryFile();

  if(history_fd < 0){
    return 0;
  }

  int i = 0;

  while(read(history_fd, &history[currHistory][i], 1) > 0){
    if(history[currHistory][i] == '\n'){
      currHistory++;
      i=0;
    }
    else{
      i++;
    }
  }

  for(int i=0;i<50;i++){
    if(history[i][0] != '\0')
      trimNewLine(history[i]);
  }

  if(currHistory > 49){
    currHistory = 49;
  }

  close(history_fd);

  return 1;
}

int storeHistory(char *hist){
  if(currHistory < 0 || currHistory > 49){
    return 0;
  }
  
  if(*hist == '\n' || *hist == '\0' || *hist == ' '){
    return 0;
  }

  currHistoryOffset = 0;

  if(currHistory >= 49){
    currHistory = 49;
    //shift everyone up 1
    for(int i=0;i<=48;i++){
      memcpy(history[i],history[i+1], MAX_INPUT);
    }
    memset(history[49], 0, MAX_INPUT);
  }

  memcpy(history[currHistory], hist, MAX_INPUT);

  if(currHistory < 48){
    currHistory++;
  } 

  return 1;
}

//0 = up/prev. 1 = down/next.
int loadHistory(char *buffer, int direction){
  if(direction == 0){

    memset(buffer, 0, MAX_INPUT);

    if(currHistoryOffset+currHistory <= 0){
      //DO NOT DECREMENT
      return 1;
    }
    else{
      currHistoryOffset--;
    }

    memcpy(buffer, history[currHistory+currHistoryOffset], MAX_INPUT);
    trimNewLine(buffer);

    return 1;
  }
  else if(direction == 1){

    memset(buffer, 0, MAX_INPUT);

    if(currHistoryOffset+currHistory >= 49){
      //DO NOT INCREMENT
      return 1;
    }
    else{
      currHistoryOffset++;
    }

    memcpy(buffer, history[currHistory+currHistoryOffset], MAX_INPUT);
    trimNewLine(buffer);

    return 1;

  }
  else{
    return 0;
  }
}

void getPaths(char **envp)
{

  //Grab all paths (separated by colons), put them in allPaths
  char *name = "PATH";
  char *allPaths;
  allPaths = getenv(name);
  //printf("All Paths: %s\n", allPaths);

  //Split them into individual strings. Store in paths

  char* temp = strtok(allPaths, ":");

  //char* pathList[20];
  while ((temp = strtok(NULL, ":")) != NULL)
  {
    //printf("Next Path: %s\n", temp);
    paths[pathSize++] = temp;
    
  }
}

void setBuiltInCommand(char* cmd)
{
  trimNewLine(cmd);

  //Check only first word for the command
  //+5 because I get a stack smashing error otherwise. No idea why
  char cmdCmp[MAX_PATH_LENGTH];
  strcpy(cmdCmp, cmd);
  
  strtok(cmdCmp, " ");

  //printf("cmd: %s, cmpCmp: %s\n", cmd, cmdCmp);
  //Check for empty command
  if(strcmp(cmdCmp, "")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for exit command
  if(strcmp(cmdCmp, "exit")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for cd command
  if(strcmp(cmdCmp, "cd")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for pwd command
  if(strcmp(cmdCmp, "pwd")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for echo command
  if(strcmp(cmdCmp, "echo")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for set command
  if(strcmp(cmdCmp, "set")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for help command
  if(strcmp(cmdCmp, "help")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for jobs command
  if(strcmp(cmdCmp, "jobs")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for fg command
  if(strcmp(cmdCmp, "fg")==0)
  {
    builtInCommand = 1;
    return;
  }

  //Check for bg command
  if(strcmp(cmdCmp, "bg")==0)
  {
    builtInCommand = 1;
    return;
  }

  if(strcmp(cmdCmp, "history")==0){
    builtInCommand = 1;
    return;
  }

  if(strcmp(cmdCmp, "clear-history")==0){
    builtInCommand = 1;
    return;
  }

  //If we get here without breaking out of the function, make sure the flag is turned off
  builtInCommand = 0;
}

void executeBuiltInCommand(char* cmd, char** envp)
{
  //Trim the newline character
  trimNewLine(cmd);

  //Check for the first part of the command
  char cmdCmp[sizeof(cmd)+5];
  strcpy(cmdCmp, cmd);
  //printf("cmd: %s, cmpCmp: %s\n", cmd, cmdCmp);
  strtok(cmdCmp, " ");

  //Check which function it is

  if(strcmp(cmdCmp, "")==0)
  {
    return;
  }
  if(strcmp(cmdCmp, "exit")==0)
  {
    printf("EXITING\n");
    storeHistoryToFile();
    exit(EXIT_SUCCESS);
  }
  if(strcmp(cmdCmp, "cd")==0)
  {
    //Make it change dir using chdir
    executeCD(cmd, envp);
  }
  if(strcmp(cmdCmp, "pwd")==0)
  {
    //This should not be 100
    char currentPath[MAX_PATH_LENGTH];
    printf("Path: %s\n", getcwd(currentPath, MAX_PATH_LENGTH));
  }
  if(strcmp(cmdCmp, "echo")==0)
  {
    executeEcho(cmd);
  }
  if(strcmp(cmdCmp, "set")==0)
  {
    executeSet(cmd, envp);
  }
  if(strcmp(cmdCmp, "help")==0)
  {
    //Print help menu
    printHelpMenu();
  }
  if(strcmp(cmdCmp, "jobs")==0)
  {
    printBackgroundJobList();
  }
  if(strcmp(cmdCmp, "fg")==0)
  {
    fg(cmd);
  }

  if(strcmp(cmdCmp, "history")==0){
    printHistory();
  }

  if(strcmp(cmdCmp, "clear-history")==0){
    clearHistoryFile(); 
  }

  //Reset the built in command flag
  builtInCommand = 0;
}

//Calls a program. If given a / in the command, uses it as a direct address, otherwise searches for it in the path
void callProgram(char* cmd, struct process* exeProcess, char** argv, char** envp, int fileIn, int fileOut, int pgid, int bg)
{
  //Trim the newline character
  trimNewLine(cmd);

  //Set process ids
  int pid;
  pid = getpid ();
  if (pgid == 0) 
    pgid = pid;
  setpgid (pid, pgid);

  //Put in forground
  //if (!bg)
    //tcsetpgrp (0, pgid);


  //int pid = fork();

  //if(pid==0)
  {
    //We're in the child proccess
    struct stat sb;

    //fprintf(stderr, "fileIn:%d\n", fileIn);
    if(fileIn != STDIN_FILENO)
    {
      int in = STDIN_FILENO;
      //Handle numbered in redirect
      if(exeProcess->inputFd!=-1)
      in = exeProcess->inputFd;

      dup2(fileIn, in);
      close(fileIn);
    }

    //fprintf(stderr, "fileOut:%d\n", fileOut);
    if(fileOut != STDOUT_FILENO)
    {
      int out = STDOUT_FILENO;
      //Handle numbered out redirect
      if(exeProcess->outputFd!=-1)
      out = exeProcess->outputFd;

      dup2(fileOut, out);
      close(fileOut);
    }

    //Pass it in directly if the cmd contains a slash
    //Testing first command
    char cmdCmp[strlen(cmd)];
    getFirstCommand(cmd, cmdCmp);

    if(containsSlash(cmdCmp))
    {
     if (stat(cmdCmp, &sb) == -1) 
        {
           //perror("stat");
        }
        else
        {
          //We found the path!
          //printf("File Found: %s\n", cmdCmp);

          //Get the new argv sting array to pass to execve
          char *newArgv[MAX_ARGV_LENGTH];
          int length = 0;
          getArgv(cmd, newArgv, &length);
          //printStringArray(newArgv, length);

          if(debug)
            fprintf(stderr, "RUNNING: %s\n", cmdCmp);
          execve(cmdCmp, newArgv, envp);

          //free(newArgv);
        }
    }

    //If it doesn't contain a slash, we need to search for the file in all paths
    else
    {
      //This should not be 100
      char tempPath[MAX_PATH_LENGTH];

      //Check all available paths in the PATH environment variable
      for(int i = 0; i <pathSize; i++)
      {
        strcpy(tempPath, paths[i]);
        strcat(tempPath,"/");
        strcat(tempPath, cmdCmp);
        //printf("Testing Path: %s\n", tempPath);
        if (stat(tempPath, &sb) == -1) 
        {
           //perror("stat");
          //fprintf(stderr, "StatErr\n");
        }
        else
        {
          //We found the path!
          //fprintf(stderr, "Path Found: %s\n", tempPath);

          //Get the new argv sting array to pass to execve
          char *newArgv[MAX_ARGV_LENGTH];
          int length = 0;
          getArgv(cmd, newArgv, &length);

          if(debug)
            fprintf(stderr, "RUNNING: %s\n", cmdCmp);
          execve(tempPath, newArgv, envp);

          //free(newArgv);
        }
      }
    }
    //If we got here, we failed to call execve
    //fprintf(stderr, "File Not Found\n");
    //We need to exit the child proccess

    exit(EXIT_SUCCESS);
    

  }
  //We're in the parent process. Wait for child to finish before continuing.
  //int status;
  //wait(&status);
  //printf("Status: %d\n", status);


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

int substring(char *bigString, char *subdest, int begin, int end){
  int size = strlen(bigString);
  if(begin < 0 || begin > size-1 || end < 0 || end > size){
    return 0;
  }
  else{
    for(int i=begin;i<end;i++){
      *subdest = bigString[i];
      subdest++;  
    }
    *subdest = '\0';
    subdest -= (end-begin);
    //printf("\nsubstring of %s is %s\n\n", bigString, subdest);
    return 1; 
  }

  //printf("subdest: %s\n",subdest);
}

void tokenize(char *cmd, char **newArgv, int* length){
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

int printPrompt()
{
  // Print the prompt
    char currentPath[MAX_PATH_LENGTH];
    //printf("Path: %s\n", getcwd(currentPath, MAX_PATH_LENGTH));

    char* cwd = getcwd(currentPath, MAX_PATH_LENGTH);
    write(1, "[", 1);
    write(1, cwd, strlen(cwd));
    write(1, "] ", 2);
   int rv = write(1, prompt, strlen(prompt));
   return rv;
}

int getPromptSize(){
  char currentPath[MAX_PATH_LENGTH];
  char* cwd = getcwd(currentPath, MAX_PATH_LENGTH);
  return 1+strlen(cwd)+2+strlen(prompt);
}

int containsSlash(char* cmd)
{
  size_t len = strlen(cmd);
  size_t spn = strcspn(cmd, "/");

  if (spn != len) 
  {
    //printf("SLASH\n\n");
    return 1;
  }
  return 0;
}

//Grabs the first part of a command Ex. "ls" in "ls -l"
char* getFirstCommand(char* cmd, char* cmdCmp)
{
  //Check for the first part of the command
  strcpy(cmdCmp, cmd);
  //printf("cmd: %s, cmpCmp: %s\n", cmd, cmdCmp);
  strtok(cmdCmp, " ");
  //printf("CMDCMP: %s\n", cmdCmp);

  return cmdCmp;
}


void getArgv(char* cmd, char** newArgv, int* length)
{
    //Split them into individual strings. Store in paths

  char* tempstr = calloc(strlen(cmd)+1, sizeof(char));
  strcpy(tempstr, cmd);

  char* temp = strtok(tempstr, " ");

  //printf("Token: %s\n", temp);

  newArgv[*length] = temp;
  //char* pathList[20];
  while ((temp = strtok(NULL, " ")) != NULL)
  {
    (*length)++;
    newArgv[*length] = temp;
    //printf("Token: %s\n", temp);
      
  }
  //Increment length once more so it's actual length
  (*length)++;
  //Terminate with null pointer
  newArgv[*length] = NULL;
  //printf("Length: %d\n", *length);

}

void printStringArray(char** arr, int length)
{
  printf("String Array Length: %d\nString:", length);

  for(int i = 0; i < length; i++)
  {
    printf("\n%s ", arr[i]);

  }
  printf("\n");
}

void printHelpMenu()
{
  printf("\n\ncd [-][.][..] - changes directory\n");
  printf("pwd - prints current absolute path\n");
  printf("echo - print strings and expand environment variables\n");
  printf("set - modify existing environment variables and create new ones\n");
  printf("help - displays this menu\n\n");

}

void executeCD(char* cmd, char** envp)
{
  int length = 0;
  char* args[MAX_ARGV_LENGTH];

  getArgv(cmd, args, &length);

  //Grab HOME environment variable
  char *homeStr = "HOME";
  char *home;
  home = getenv(homeStr);


  char current[MAX_PATH_LENGTH];
  //printf("Current Dir: %s\n", getcwd(current, MAX_PATH_LENGTH));

  //printf("Current Dir: %s\n", getenv(currentStr));


  //Set directory to home
  if(length == 1)
  {
    //printf("Last Directory Start %s\n", lastDir);
    //Save current directory as last directory
    strcpy(lastDir, current); 
    chdir(home);
    //printf("Current Directory %s\n", current);
    //printf("Last Directory %s\n", lastDir);
    return;
  }

  //Set directory to last
  if(strcmp(args[1], "-")==0)
  {
    //printf("Last Directory Start %s\n", lastDir);
    //printf("Changing to last directory: %s\n", lastDir);
    chdir(lastDir);
    strcpy(lastDir, current);
    //printf("Current Directory %s\n", current);
    //printf("Last Directory %s\n", lastDir);
    return;
  }

  //If we're still here, we need to go to the path specified in args[1].
  //This should also cover cd .. and cd .
  //printf("Last Directory Start %s\n", lastDir);
  char nextDir[MAX_PATH_LENGTH];

  strcpy(nextDir, current);
  strcat(nextDir,"/");
  strcat(nextDir, args[1]);

 // printf("NextDir: %s\n", nextDir);
  chdir(nextDir);


  strcpy(lastDir, current);
  //printf("Current Directory %s\n", current);
  //printf("Last Directory %s\n", lastDir);
}

//Right now this only sets the environment variables for this process. Not sure if we need to set it for the parent process? (a harder task)
//returns 0(false) if it has invalid parameters.
int executeSet(char* cmd, char** envp)
{
  //Add length checking
  int length = 0;
  char* args[MAX_ARGV_LENGTH];


  getArgv(cmd, args, &length);

  //if(strcmp(args[4], "") == 0 && strcmp(args[2], "=") == 0){
    setenv(args[1], args[3], 1);
    //return 1;
  //}
  //else{
  //free(args);
  return 0;
  //}
}

//Tokenizes and prints out what is input. Has issues with too long strings
void executeEcho(char* cmd)
{

  int length = 0;
  char* args[MAX_ARGV_LENGTH];

  char* envirVar;

  getArgv(cmd, args, &length);
  //Start at 1 to skip first arg, which is echo

  printf("\n");

  for(int i = 1; i < length; i++)
  {
    if(args[i][0] == '$'){
      //printf("%s ",args[i]);
      removeCharFromStringWithoutIndex(args[i], '$');
      envirVar = getenv(args[i]);
      if(envirVar == NULL){
        printf("0");
      }
      else{
        printf("%s ",envirVar);
      }
    }
    else{
      printf("%s ", args[i]);
    }

  }
  printf("\n");

  //free(args);
}

void parseIntoProcesses(char* cmd, struct job* newJob)
{
  int argsLength = 0;
  //Contains a list of all args passed in from terminal
  char* args[MAX_ARGV_LENGTH];

  getArgv(cmd, args, &argsLength);

  struct process* initialProcess = calloc(sizeof(struct process),1);
  initialProcess->full = 0;
  initialProcess->nextProcess = NULL;

  initialProcess->inputFd = -1;
  initialProcess->outputFd = -1;
  //struct job* newJob = calloc(sizeof(struct job), 1);

  
  //Go through every arg in args.
  //Create new processes when needed, link them together properly.
  char* currentCommand[1000];
  int counter = 0;

  //This is 1 if the next read arg is an output file.
  int redirectionFlag = 0;
  for(int i = 0; i < argsLength; i++)
  {
    //What we read is the current process's output redirection
    if(redirectionFlag!=0)
    {
      struct process* tempProcess = initialProcess;
        while(tempProcess->nextProcess != NULL)
          tempProcess = tempProcess->nextProcess;

      //int in = -1;
      int out = -1;
      if(redirectionFlag==1)
      {
        tempProcess->redirectOutput = args[i];
        out = containsOutputRedirect(args[i-1]);
        if(out==0)
          out = -1;
        //printf("OUT: %d\n", out);
        tempProcess->outputFd = out;
      }
      else if(redirectionFlag==2)
      {
        tempProcess->redirectInput = args[i];
        int in = containsInputRedirect(args[i-1]);
        //printf("IN: %d\n", in);
        tempProcess->inputFd = in;
        if(in==0)
          in = -1;
      }

      //printf("Out or In set - Out: %d, In: %d\n", out, in);
      redirectionFlag = 0;
      continue;
    }
    
      //containsOutputRedirect(args[i]);

    //Output redirection detection
    if((containsOutputRedirect(args[i])!=-1 || containsInputRedirect(args[i])!=-1 || strcmp(args[i], "|")==0) || i == argsLength-1)
    {

      //Handle final str case
      if(i==argsLength-1) 
      {
        currentCommand[counter] = args[i];
        counter++;
      }

      //printf("Found Special Symbol");
      char* tempStr[MAX_ARGV_ROWS];
      calloc2dArray(tempStr, MAX_ARGV_ROWS, MAX_ARGV_ROWS);
      strArrayCpy(tempStr, currentCommand, counter);

      struct process* currentProcess = calloc(sizeof(struct process),1);
      currentProcess->inputFd = -1;
      currentProcess->outputFd = -1;

      currentProcess->argv = calloc(counter, sizeof(char*));
      calloc2dArray(currentProcess->argv, counter, MAX_ARGV_ROWS);

      strArrayCpy(currentProcess->argv, tempStr, counter);
      //currentProcess->argv = tempStr;
      currentProcess->argvLength = counter;
      currentProcess->full=1;

      //Clear the current command
      currentCommand[0] = NULL;
      counter = 0;

      //printProcess(currentProcess);
      //If this is the first process
      if(initialProcess->full==0)
        initialProcess = currentProcess;

      //Not the first process
      else if(currentProcess->argvLength!=0)
      {
        //printf("Not Initial Process\n");
        struct process* tempProcess = initialProcess;
        while(tempProcess->nextProcess != NULL)
        {
          //printf("Non Null Next\n");
          tempProcess = tempProcess->nextProcess;
        }
        tempProcess->nextProcess = currentProcess;
      }
      //Set the redirection flag

      
      if(containsOutputRedirect(args[i])!=-1)
        redirectionFlag = 1;
      else if(containsInputRedirect(args[i])!=-1)
        redirectionFlag = 2;
      else if(strcmp(args[i], "|")==0)
        redirectionFlag = 0;


    }

    else
    {
      currentCommand[counter] = args[i];

      counter++;
    }
  }
  newJob->firstProcess = initialProcess;
  //printJob(newJob);
/*
  int pipefd[2];

  //Exit if we fail to pipe
  if (pipe(pipefd) == -1) 
  {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  printf("PipeFD[0]: %d, pipefd[1]: %d\n", pipefd[0], pipefd[1]);
  */
}

void strArrayCpy(char** destStrArr, char** cpyStrArr, int cpyLength)
{
  for(int i = 0; i < cpyLength; i++)
  {
    //destStrArr[i] = cpyStrArr[i];
    //printf("cpyLength: %d\n", cpyLength);
    strcpy(destStrArr[i], cpyStrArr[i]);
  }
  //destStrArr[cpyLength+1] = NULL;
  //strcpy(destStrArr[cpyLength], NULL);
}

void printProcess(struct process* printProcess)
{
  printStringArray(printProcess->argv, printProcess->argvLength);
}

void calloc2dArray(char** arr, int rows, int length)
{
  //arr = calloc(rows,sizeof(char*));

  for(int i = 0; i < rows; i++)
  {
    arr[i] = calloc(length, sizeof(char));
  }

}

void printJob(struct job* printJob)
{

  int counter = 0;
  struct process* tempProcess = printJob->firstProcess;

  printf("\n---PROCESS PRINTING---\n\n");
  //Print First Process
  printf("PROCESS: %d\n", counter);
  printStringArray(tempProcess->argv, tempProcess->argvLength);
  printf("Input Redirect: %s\n", tempProcess->redirectInput);
  printf("Output Redirect: %s\n", tempProcess->redirectOutput);
  printf("Input FD: %d\n", tempProcess->inputFd);
  printf("Output FD: %d\n", tempProcess->outputFd);

  //Print the rest
  while(tempProcess->nextProcess != NULL)
  {
    counter++;
    tempProcess = tempProcess->nextProcess;
    printf("PROCESS: %d\n", counter);
    printStringArray(tempProcess->argv, tempProcess->argvLength);
    printf("Input Redirect: %s\n", tempProcess->redirectInput);
    printf("Output Redirect: %s\n", tempProcess->redirectOutput);
    printf("Input FD: %d\n", tempProcess->inputFd);
    printf("Output FD: %d\n", tempProcess->outputFd);
  }

  printf("\n\n");
}

void executeJob(struct job* newJob, char** envp)
{
  struct process* tempProcess = newJob->firstProcess;
  char cmd[MAX_INPUT];
  memset(cmd, 0, MAX_INPUT);

  int pipes[2];
  int fileIn;
  int fileOut;

  fileIn = STDIN_FILENO;
  while(tempProcess!=NULL)
  {
    //Set fileIn to std out by default
    fileOut = STDOUT_FILENO;

    //printf("PIPING\n");
    pipe(pipes);

    if(tempProcess->nextProcess!=NULL)
      fileOut = pipes[1];
    

    reconstructCmd(cmd, tempProcess->argv, tempProcess->argvLength);
    setBuiltInCommand(cmd);
    //If it is, execute it as a built in function
    if(builtInCommand==1)
      executeBuiltInCommand(cmd, envp);
    //Otherwise, try to call it as a program

    else 
      {
        int pid = fork();
        if(pid==0)
        {
          //In child

          //If current process has a special redirect, set it here
          if(tempProcess->redirectInput!=NULL)
          {
            //printf("Redir Input: %s\n", tempProcess->redirectInput);
            int inputFd = open(tempProcess->redirectInput,O_RDONLY);
            fileIn = inputFd;
          }
          if(tempProcess->redirectOutput!=NULL)
          {
            //printf("Redir Output: %s\n", tempProcess->redirectOutput);
            int outputFd = open(tempProcess->redirectOutput, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
            fileOut = outputFd;
          }

          //Set pid
          
          int bg = newJob->bg;
          callProgram(cmd, tempProcess, tempProcess->argv, envp, fileIn, fileOut, newJob->pgid, bg);
        }
        else
        {
          //In parent
          if (!newJob->pgid)
            newJob->pgid = pid;
          setpgid (pid, newJob->pgid);

          //printf("Process ID: %d\n", getpid());

          //printf("WAITPID: %d\n", pid);

          int status = -1;
          //Wait if in foreground
          if(!newJob->bg)
            wait(&status);

          if(!newJob->bg)
          {
            //printf("Trying to remove \n");
            checkAndRemoveFinalProcess(newJob, tempProcess);
          }

          else  
            {
              //In background
            }
          
          if(debug)
           fprintf(stderr, "ENDED: %s (ret=%d)\n", cmd, status);

          /* Clean up after pipes.  */
          if (fileIn != STDIN_FILENO)
            close (fileIn);
          if (fileOut!= STDOUT_FILENO)
            close (fileOut);
          fileIn = pipes[0];

          
        }
      }

      

    memset(cmd, 0, sizeof(cmd)); //resets the cmd string
    tempProcess = tempProcess->nextProcess;
  }
}

void reconstructCmd(char* cmd, char** argv, int argvLength)
{
  trimNewLine(cmd);
  for(int i = 0; i < argvLength; i++)
  {
    strcat(cmd, argv[i]);
    if(i == argvLength-1)
      strcat(cmd, "\0");
    else strcat(cmd, " ");
  }

}

int containsOutputRedirect(char* arg)
{
  //printf("(OUTPUT) String: %s\n", arg);
  char* location = strstr(arg, ">");
  //printf("(OUTPUT) Location: %p\n", location);

  //Not found!
  if(location==NULL)
    return -1;

  int length = location - arg;
  //printf("Length: %d\n", length);

  char newArg[strlen(arg)];
  substring(arg, newArg, 0, length);
  //printf("Substring: %s\n", newArg);

  int returnInt = atoi(newArg);

  return returnInt;
}
int containsInputRedirect(char* arg)
{
  //printf("(INPUT) String: %s\n", arg);
  char* location = strstr(arg, "<");
  //printf("(INPUT) Location: %p\n", location);

  //Not found!
  if(location==NULL)
    return -1;

  int length = location - arg;
  //printf("Length: %d\n", length);

  char newArg[strlen(arg)];
  substring(arg, newArg, 0, length);
  //printf("Substring: %s\n", newArg);

  int returnInt = atoi(newArg);

  return returnInt;
}

void setDebugStatus(char** argv, int length)
{
  for(int i = 0; i < length ; i++)
  {
    if(strcmp(argv[i], "-d")==0)
    {

      debug = 1;
      printf("DEBUGGING ENABLED\n");
    }
  }
}

void controlC()
{
  write(1, "Control C\n", 10);

  struct job* tempJob = jobHead;
  while(tempJob!=NULL)
  {
    //printf("Loop\n");
    if(tempJob->bg==0)
    {
      printf("Kill PGID: %d\n", tempJob->pgid);
      kill(tempJob->pgid, SIGKILL);
    }

    tempJob = tempJob->nextJob;
  }
}
void controlZ()
{
  write(1, "Control Z\n", 10);

  struct job* tempJob = jobHead;
  while(tempJob!=NULL)
  {
    //printf("Loop\n");
    if(tempJob->bg==0)
      kill(tempJob->pgid, SIGTSTP);

    tempJob = tempJob->nextJob;
  }
  
}

int parseForAnpersand(char** argv, int length)
{
  for(int i = 0; i < length; i++)
  {
    if(strcmp(argv[i], "&")==0)
    {
      argv[i][0] = '\0';
      return 1;
    }
  }
  return 0;
}

void addJobToList(struct job* newJob)
{

  //Scan for & background character

  scanForBackgroundCharacter(newJob);

  //Assign JID
  newJob->jpid = 0;
  assignJid(newJob);

  if(jobHead==NULL)
    jobHead = newJob;

  
  else
  {
    struct job* tempJob = jobHead;

    while(tempJob->nextJob != NULL)
      tempJob = tempJob->nextJob;

    tempJob->nextJob = newJob;
  }
}

void printJobList()
{
  struct job* tempJob = jobHead;

  while(tempJob!=NULL)
  {
    printJob(tempJob);
    tempJob = tempJob->nextJob;
  }
}

void printBackgroundJobList()
{
  struct job* tempJob = jobHead;

  printf("\nPrinting BG JOBS:\n\n");
  while(tempJob!=NULL)
  {
    if(tempJob->bg==1)
      printf("NAME: %s, JID: %d, PID: %d\n", tempJob->firstProcess->argv[0], tempJob->jpid, tempJob->firstProcess->pid);
    tempJob = tempJob->nextJob;
  }
  printf("\n");
}

void scanForBackgroundCharacter(struct job* newJob)
{

  struct process* tempProcess = newJob->firstProcess;
  while(tempProcess!=NULL)
  {
    int bg = parseForAnpersand(tempProcess->argv, tempProcess->argvLength);
    //printf("Set BG: %d\n", bg);
    newJob->bg = bg;
    tempProcess = tempProcess->nextProcess;
  }
}

void assignJid(struct job* newJob)
{
  struct job* tempJob = jobHead;

  int counter;
  if(tempJob!=NULL)
    counter = tempJob->jpid;
  else counter = 0;

  while(tempJob!=NULL)
  {
    if(counter<tempJob->jpid)
    {
      counter = tempJob->jpid;
      continue;
    }
    tempJob = tempJob->nextJob;
  }
  //Add one to it;
  counter++;

  //Set it
  newJob->jpid = counter;
  //printf("Set jpid: %d\n", counter);
}

void fg(char* cmd)
{
  struct job* tempJob = jobHead;

  char *newArgv[MAX_ARGV_LENGTH];
  int length = 0;
  getArgv(cmd, newArgv, &length);

  int jid = atoi(newArgv[1]);

  while(tempJob!=NULL)
  {

    int status;
    if(tempJob->jpid == jid)
      waitpid(tempJob->firstProcess->pid, &status, 0);
    tempJob= tempJob->nextJob;
  }
}

void checkAndRemoveFinalProcess(struct job* job, struct process* process)
{
  if(process->nextProcess==NULL)
  {
    //Remove from job list
    struct job* tempJob = jobHead;


    if(jobHead == job)
      jobHead = job->nextJob;

    else
    {
      while(tempJob!=NULL)
      {
        if(tempJob->nextJob == job)
        {
          tempJob->nextJob = job->nextJob;
          return;
        }
        tempJob = tempJob->nextJob;
      }
    }
  }
}

