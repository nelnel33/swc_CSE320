#include "sfwrite.h"

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

/**
* Uses a mutex to lock an output stream so it is not interleaved when
* printed to by different threads.
* @param lock Mutex used to lock output stream.
* @param stream Output stream to write to.
* @param fmt format string used for varargs.
*/
void sfwrite(pthread_mutex_t *lock, FILE* stream, char *fmt, ...){
	pthread_mutex_lock(lock);

	va_list ap;

	va_start(ap, fmt);

	vfprintf(stream, fmt, ap);
	va_end(ap);

	pthread_mutex_unlock(lock);
}

/*

void* callback(void *nothing){

	printf("hello");

	while(1){
		sfwrite(&m, stdout, "TID: %s\n", nothing);
		sleep(1);
	}

	return NULL;
}
int main(int argc, char** argv){
	printf("entered main");

	pthread_t one,two,three,four,five,six;

	pthread_create(&one, NULL, callback, "1");
	pthread_create(&two, NULL, callback, "2");
	pthread_create(&three, NULL, callback, "3");
	pthread_create(&four, NULL, callback, "4");
	pthread_create(&five, NULL, callback, "5");
	pthread_create(&six, NULL, callback, "6");

	while(1);
	
}
*/