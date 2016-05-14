#define __USE_XOPEN
#define _GNU_SOURCE
#include "parser.h"

#define MAX_INPUT 1024

//Globals
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct audit* audit_head;


struct audit* getAuditHead(){
	return audit_head;
}

void fillAudit(char* auditFileName)
{
	freeAudit();
	audit_head = NULL;
	FILE* auditFile = fopen(auditFileName, "a+");
	int auditFd = fileno(auditFile);

	char* last_char = calloc(1,1);
	char* message = calloc(MAX_INPUT, sizeof(char));

	flock(auditFd, LOCK_EX);

	while(read(auditFd, last_char, sizeof(last_char)>0))
	{
		strcat(message, last_char);

		//If new line
		if(last_char[0]=='\n')
		{

			parseMessage(message);
			memset(message, 0, MAX_INPUT);
		}
	}

	flock(auditFd, LOCK_UN);
	free(last_char);
	free(message);

}

void freeAudit()
{
	struct audit* moveAudit = audit_head;
	while(moveAudit!=NULL)
	{
		struct audit* tempAudit = moveAudit;
		moveAudit = moveAudit->next;

		free(tempAudit);
	}
}

void parseMessage(char* message)
{
	//printf("Message: %s\n", message);

	char* firstComma = strchr(message, ',');
	char* secondComma = strchr(firstComma+1, ',');
	char* thirdComma = strchr(secondComma+1, ',');
	char* end = strchr(message, '\n');



	//printf("Comma %p, %p, %p\n", firstComma, secondComma, thirdComma);

	struct audit* newAudit = calloc(1, sizeof(audit));

	char* newTime = calloc(MAX_INPUT, sizeof(char));
	strncpy(newTime, message, firstComma-message);
	strcpy(newAudit->time, newTime);
	//printf("NEWTIME: %s\n", newAudit->time);

	char* newName = calloc(MAX_INPUT, sizeof(char));
	strncpy(newName, firstComma+2, secondComma-firstComma-2);
	strcpy(newAudit->name, newName);
	//printf("NEWNAME: %s\n", newAudit->name);

	char* newCmd = calloc(MAX_INPUT, sizeof(char));
	strncpy(newCmd, secondComma+2, thirdComma-secondComma-2);
	strcpy(newAudit->cmd, newCmd);
	//printf("NEWCMD: %s\n", newAudit->cmd);

	char* newInfo = calloc(MAX_INPUT, sizeof(char));
	strncpy(newInfo, thirdComma+2, end-thirdComma-2);
	strcpy(newAudit->info, newInfo);
	//printf("NEWINFO: %s\n", newAudit->info);

	insertIntoAudit(newAudit);

}

void insertIntoAudit(struct audit* newAudit)
{
	struct audit* auditTemp = audit_head;

	if(audit_head==NULL)
	{
		audit_head = newAudit;
		return;
	}

	while(auditTemp->next!=NULL)
	{
		auditTemp = auditTemp->next;

	}
	auditTemp->next = newAudit;
}

void printAuditList(struct audit* auditToPrint)
{
	struct audit* auditTemp = auditToPrint;

	if(auditTemp==NULL)
	{
		return;
	}

	printf("\n\nNEW AUDIT PRINT\n\n");
	while(auditTemp!=NULL)
	{
		printf("AUDIT TIME: %s, NAME: %s, CMD: %s, INFO: %s\n", auditTemp->time, auditTemp->name, auditTemp->cmd, auditTemp->info);
		auditTemp = auditTemp->next;
	}
}

/*
	Takes in column (0=time, 1=name, 2=cmd, 3=info), and if the list should be in ascending or descending order
	Returns audit list with all audits sorted as specified.
*/

struct audit* sortBy(int column, int ascending)
{
	struct audit* auditTemp = audit_head;

	struct audit* returnHead = calloc(1, sizeof(audit));
	//Copy the head
	//copyAudit(returnHead, auditTemp);

	//Start inserting
	while(auditTemp!=NULL)
	{
		returnHead = insertSorted(column, ascending, returnHead, auditTemp);

		//printf("auditTemp: %s\n", auditTemp->name);
		auditTemp = auditTemp->next;
		
	}

	return returnHead;
}

/*
	Takes in column (0=time, 1=name, 2=cmd, 3=info), and a filter string. 
	Returns audit list with only audits that contain the string as part of the specified column.
*/

struct audit* filterBy(int column, char* filter)
{
	struct audit* auditTemp = audit_head;

	struct audit* returnHead = calloc(1, sizeof(audit));

	while(auditTemp!=NULL)
	{
		if(checkFilter(column, filter, auditTemp))
			returnHead = insertSorted(column, 1, returnHead, auditTemp);
		auditTemp = auditTemp->next;
	}

	return returnHead;
}

/*
	Takes a filter
	Returns audit list with only audits containing that filter
*/

struct audit* searchByKeyword(char* filter)
{
	struct audit* auditTemp = audit_head;

	struct audit* returnHead = calloc(1, sizeof(audit));

	while(auditTemp!=NULL)
	{
		if(checkKeyword(filter, auditTemp))
			returnHead = insertSorted(0, 1, returnHead, auditTemp);
		auditTemp = auditTemp->next;
	}

	return returnHead;
}

/*
	Takes in 2 strings of the format we use for time fields. 
	Returns an audit list with only audits that fall between the 2 times.
*/

struct audit* filterByTimeRange(char* startTime, char* endTime)
{
	struct audit* auditTemp = audit_head;

	struct audit* returnHead = calloc(1, sizeof(audit));

	while(auditTemp!=NULL)
	{
		if(checkTimeRange(startTime, endTime, auditTemp))
			returnHead = insertSorted(0, 1, returnHead, auditTemp);
		auditTemp = auditTemp->next;
	}

	return returnHead;

}

int checkTimeRange(char* startTime, char* endTime, struct audit* auditTemp)
{

	time_t start = parseTime(startTime);
	time_t end = parseTime(endTime);
	time_t current = parseTime(auditTemp->time);

	//printf("Start: %f, End: %f\n", difftime(start, current), difftime(current,end));
	if(difftime(current,end)<=0 && difftime(start, current)<=0)
		return 1;

	return 0;
}



int checkFilter(int column, char* filter, struct audit* auditTemp)
{
	if(strstr(auditTemp->time, filter)==NULL && column==0)
		return 0;

	if(strstr(auditTemp->name, filter)==NULL && column==1)
		return 0;

	if(strstr(auditTemp->cmd, filter)==NULL && column==2)
		return 0;

	if(strstr(auditTemp->info, filter)==NULL && column==3)
		return 0;

	return 1;
}

int checkKeyword(char* filter, struct audit* auditTemp)
{
	if(strstr(auditTemp->time, filter)!=NULL)
		return 1;

	if(strstr(auditTemp->name, filter)!=NULL)
		return 1;

	if(strstr(auditTemp->cmd, filter)!=NULL)
		return 1;

	if(strstr(auditTemp->info, filter)!=NULL)
		return 1;

	return 0;
}

struct audit* insertSorted(int column, int ascending, struct audit* returnHead, struct audit* insertAudit)
{
	struct audit* temp = returnHead;
	struct audit* newInsertAudit = calloc(1, sizeof(audit));
	copyAudit(newInsertAudit, insertAudit);

	if(newInsertAudit->name[0]=='\0')
	{
		//printf("NULL STUFF\n");
		return returnHead;;
	}


	if(returnHead==NULL)
	{
		returnHead = newInsertAudit;
		//printf("RETURNHEAD NULL\n");
		return returnHead;;
	}

	if(ascending==1)
	{
		while(compare(column, newInsertAudit, temp)>0)
		{
			if(temp->next!=NULL)
				temp = temp->next;
			else break;
		}
	}
	else
	{
		while(compare(column, newInsertAudit, temp)<0)
		{
			if(temp->next!=NULL)
				temp = temp->next;
			else break;
		}
	}
	
	//printf("temp: %s, insert: %s\n", temp->name, insertAudit->name);
	//if(temp->name[0]!='\0')
	newInsertAudit->next = temp;

	if(temp->prev!=NULL)
	{
		temp->prev->next = newInsertAudit;
		newInsertAudit->prev = temp->prev;

	}
	temp->prev = newInsertAudit;

	if(temp==returnHead)
		returnHead = temp->prev;

	return returnHead;
}

int compare(int column, struct audit* newInsertAudit, struct audit* temp)
{
	int cmp = 0;

	if(column==0)
	{
		time_t newTime = parseTime(newInsertAudit->time);
		time_t tempTime = parseTime(temp->time);


		double diffTime = difftime(newTime, tempTime);
		//printf("newTime: %s, oldTime: %s\n", newInsertAudit->time, temp->time);
		//printf("Compare difftime: %f\n", diffTime);
		if(diffTime>0 && temp->time[0] != '\0')
			cmp = 1;
		else cmp = 0;
		//printf("CMP: %d\n", cmp);
	}
	if(column==1)
		cmp = strcmp(newInsertAudit->name, temp->name);
	if(column==2)
		cmp = strcmp(newInsertAudit->cmd, temp->cmd);
	if(column==3)
		cmp = strcmp(newInsertAudit->info, temp->info);


	return cmp;
}

void copyAudit(struct audit* newAudit, struct audit* oldAudit)
{
	strcpy(newAudit->time, oldAudit->time);
	strcpy(newAudit->name, oldAudit->name);
	strcpy(newAudit->cmd, oldAudit->cmd);
	strcpy(newAudit->info, oldAudit->info);
}

time_t parseTime(char* time)
{
	//printf("Time: %s\n", time);

	struct tm tm;

	memset(&tm, 0, sizeof(struct tm));
	strptime(time,"%D-%I:%M%P", &tm);

	char newTime[50];
	strftime(newTime, sizeof(newTime), "%D-%I:%M%P", &tm);
	//printf("NEWTIME: %s\n", newTime);

	time_t t = mktime(&tm);

	return t;

}

//Commands to use are sortBy, filterBy, and filterByTimeRange. Implementation is mentioned above each function
/*
int main(int argc, char **argv, char** envp)
{

	fillAudit("audit.log");

	printAuditList(audit_head);

	//struct audit* sortAudit = sortBy(1, 1);
	//struct audit* filterAudit = filterBy(3, "success");
	struct audit* timeAudit = filterByTimeRange("05/06/16-10:47pm", "05/06/16-10:52pm");

	printAuditList(timeAudit);

	fillAudit("audit.log");
	//struct audit* newAudit = filterByTimeRange("05/06/16-10:50pm", "05/06/16-10:51pm");
	struct audit* searchAudit = searchByKeyword("success");
	printAuditList(searchAudit);

	return 0;
}
*/