
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

extern int errno;

int main (int argc, char *argv[])
{
	int fd;
	openlog(argv[0], LOG_PERROR, LOG_USER);
	if (argc < 3)
	{
		syslog(LOG_ERR, "Command Usage Error: USAGE: writer <FILENAME> <STRING>\n");
		closelog();
		return 1;
	}

	fd = open(argv[1], O_CREAT|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
	if ( -1 == fd )
	{
	 syslog(LOG_ERR, "%s",strerror(errno));
	 closelog();
	 return 1;
	}
	syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
	size_t bytes_written = write(fd, argv[2], strlen(argv[2]));
	if(bytes_written == -1)	{
	   perror("Write to file failed");
	   close(fd);
	   closelog();
	   exit(EXIT_FAILURE);
	 }
	 else if(bytes_written != strlen(argv[2])){
	 perror("Partial Data written to file\n");
	 close(fd);
	 closelog();
	 exit(EXIT_FAILURE);
	}
	//syslog(LOG_DEBUG, "Bytes Written: %ld",bytes_written);
	close(fd);
	closelog();	
	return 0;
}


