#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

int main( int argc, char **argv ) 
{
  int length; int count = 0;
  int fd;
  int wd;
  char buffer[BUF_LEN];

  fd = inotify_init();

  if ( fd < 0 ) {
    perror( "inotify_init" );
  }

  wd = inotify_add_watch( fd, argv[1], 
                         IN_MODIFY );

  //FILE* ffd = fdopen(fd, "w+");
  while(1){
    length = read( fd, buffer, BUF_LEN );  
    //fclean(ffd);
    lseek(fd, 0, SEEK_END);

    if ( length < 0 ) {
      perror( "read" );
    }  

    printf("%s has been modified %d times.\n", argv[1], count);
    count++;
  }

  ( void ) inotify_rm_watch( fd, wd );
  ( void ) close( fd );

  exit( 0 );
}