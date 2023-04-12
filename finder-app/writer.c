
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
	FILE *fp;
	openlog("assignment2_logs", LOG_NDELAY|LOG_PERROR|LOG_PERROR, LOG_USER);
	if (argc < 3)
	{
		syslog(LOG_ERR, "Command Usage Error: USAGE: writer <FILENAME> <STRING>\n");
		return 1;
	}

	fp = fopen(argv[1], "wb");
	if ( NULL == fp )
	{
	 syslog(LOG_ERR, "%s",strerror(errno));
	 return 1;
	}
	syslog(LOG_DEBUG, "Writing %s to %s", argv[1], argv[2]);
	fprintf(fp,"%s", argv[2]);
	fclose(fp);
	closelog();	
	return 0;
}


