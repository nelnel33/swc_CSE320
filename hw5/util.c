//CONTAINS UTILITY METHODS
//I DID NOT ADD ANY REFERENCE FROM THE OTHER CLASS
//JUST COPY AND PASTE OVER OR ADD REFERENCE IF YOU LIKE

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

void handleMsgReceived(char* cmd)
{

	int tokenLen = 0;
	char** tokenizedMsg = tokenize(cmd, &tokenLen);
	char to_name[MAX_INPUT];
	memset(to_name, 0, MAX_INPUT);

	// /chat <to> <from> <msg>

	if(strcmp(tokenizedMsg[1], username) == 0){ 
		strcat(to_name, username);
		strcat(to_name, "| Currently messaging ");
		strcat(to_name, tokenizedMsg[2]);
	}
	else{
		strcat(to_name, username);
		strcat(to_name, "| Currently messaging ");
		strcat(to_name, tokenizedMsg[1]);
	}

	free(tokenizedMsg);

	addToChats(createChat(to_name));
}