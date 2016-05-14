#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <time.h>


//Structs
typedef struct audit
{
	char time[50];
	char name[50];
	char cmd[50];
	char info[50];

	struct audit *next;
	struct audit *prev;

}audit;

void freeAudit();
void fillAudit(char* auditFileName);
void parseMessage(char* message);
void printAuditList(struct audit* auditToPrint);
struct audit* sortBy(int column, int ascending);
void sortByName(int ascending, struct audit *returnHead);
struct audit* filterByTimeRange(char* startTime, char* endTime);
struct audit* filterBy(int column, char* filter);
struct audit* searchByKeyword(char* filter);
struct audit* getAuditHead();

void insertIntoAudit(struct audit* newAudit);
void copyAudit(struct audit* newAudit, struct audit* oldAudit);
struct audit* insertSorted(int column, int ascending, struct audit* returnHead, struct audit* insertAudit);
int compare(int column, struct audit* newInsertAudit, struct audit* temp);
int checkKeyword(char* filter, struct audit* auditTemp);
int checkFilter(int column, char* filter, struct audit* auditTemp);
int checkTimeRange(char* startTime, char* endTime, struct audit* auditTemp);
time_t parseTime(char* time);