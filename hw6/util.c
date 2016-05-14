//Using as test file to implement login queue
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h> 
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <fcntl.h>
#include <arpa/inet.h>

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

typedef struct queue_node{
	struct user* user;

	struct queue_node* prev;
	struct queue_node* next;	
}queue_node;

struct queue_node* login_head = NULL; 

struct queue_node* login_tail = NULL;

struct queue_node* createQueueNode(struct user* user){
	struct queue_node* temp = calloc(1, sizeof(*temp));

	temp->user = user;

	return temp;
}

void addLoginQueue(struct user* add){

	struct queue_node* temp = createQueueNode(add);

	if(login_tail == NULL && login_head == NULL){
		login_tail = temp;
		login_head  = temp;		
	}
	else{
		temp->next = login_head;
		login_head->prev = temp;

		login_head = temp;
	}

}

struct user* popLoginQueue(){
	struct queue_node* holder = login_tail;

	if(holder != NULL){
		login_tail = login_tail->prev;
		if(holder->prev == NULL){
			login_head = NULL;
			free(holder->prev);
		}
		else{
			login_tail->next = NULL;
		}
		free(holder);
	}

	if(holder == NULL){
		return NULL;
	}
	else{
		return holder->user;
	}

}

struct user* peekLoginQueue(){
	if(login_tail == NULL){
		return NULL;
	}
	else{
		return login_tail->user; 
	}
}

void printLoginQueue(){
	struct queue_node* curr = login_head;

	printf("------LOGIN QUEUE--------\n-");

	while(curr != NULL){
		if(curr->user->name != NULL){
			printf("Name: %s\n", curr->user->name);
		}
		else{
			printf("Name: (null)\n");
		}

		curr = curr->next;
	}

	printf("-----------END---------\n");
}


int main(int argc, char** argv){
	struct user* user1 = createUser(0, NULL,"user1", -1,-1,-1,NULL,NULL);
	struct user* user2 = createUser(0, NULL,"user2", -1,-1,-1,NULL,NULL);
	struct user* user3 = createUser(0, NULL,"user3", -1,-1,-1,NULL,NULL);
	//struct user* user4 = createUser(0, NULL,"user4", -1,-1,-1,NULL,NULL);
	//struct user* user5 = createUser(0, NULL,"user5", -1,-1,-1,NULL,NULL);
	//struct user* user6 = createUser(0, NULL,"user6", -1,-1,-1,NULL,NULL);
	//struct user* user7 = createUser(0, NULL,"user7", -1,-1,-1,NULL,NULL);
	//struct user* user8 = createUser(0, NULL,"user8", -1,-1,-1,NULL,NULL);
	//struct user* user9 = createUser(0, NULL,"user9", -1,-1,-1,NULL,NULL);

	addLoginQueue(user1);
	addLoginQueue(user2);
	addLoginQueue(user3);
	//addLoginQueue(user4);
	//addLoginQueue(user5);
	//addLoginQueue(user6);
	//addLoginQueue(user7);
	//addLoginQueue(user8);
	//addLoginQueue(user9);
	

	printLoginQueue();

	popLoginQueue();
	popLoginQueue();
	//popLoginQueue();

	printLoginQueue();

	popLoginQueue();
	printLoginQueue();

	addLoginQueue(user1);
	addLoginQueue(user2);
	addLoginQueue(user3);

	printLoginQueue();

}

