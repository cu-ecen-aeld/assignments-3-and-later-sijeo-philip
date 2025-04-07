#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>

#define PORT "9000"
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"


volatile sig_atomic_t exit_requested = 0;
int server_fd = -1;


void signal_handler(int signum)
{
	syslog(LOG_INFO, "Caught signal, exiting");
	exit_requested = 1;

}

int main ( int argc, char *argv[] )
{
	int daemon_mode = 0;
	int client_fd = -1;
	int ret;

	/* Process command line arguments */
	int opt;
	while ((opt = getopt(argc, argv, "d"))!= -1) {
		switch(opt) {
			case 'd':
				daemon_mode = 1;
				break;
			default:
				fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
				exit(EXIT_FAILURE);
				break;
		}
	}


	/* Open syslog for logging*/
	openlog("aesdsocket", LOG_PID, LOG_USER);

	/*Setup signal handling for SIGINT and SIGTERM*/
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Use getaddrinfo() to obtain address(es) for binding */
	struct addrinfo hints;
	struct addrinfo *res, *rp;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;		/*Use IPv4. For IPv6 or both, Consider AF_UNSPEC */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;		/* Use Wildcard IP Address */

	ret = getaddrinfo(NULL, PORT, &hints, &res);
	if( ret != 0 ) {
		syslog(LOG_ERR, "getaddrinfo() failed: %s", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}

	/*Loop through all the results and bind to the first we can */
	for(rp = res; rp != NULL; rp = rp->ai_next) {
		server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if( server_fd == -1)
			continue;

		int yes = 1;
		if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
			syslog(LOG_ERR, "setsockopt() failed: %s", strerror(errno));
			close(server_fd);
			continue;
		}

		if (bind(server_fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break; //success
		close(server_fd);
	}
	freeaddrinfo(res);

	if( rp == NULL ) {
		syslog(LOG_ERR, "Could not bind to any address");
		exit(EXIT_FAILURE);
	}

	/* If daemon mode is requested, fork after binding */
	if( daemon_mode ) {
		pid_t pid = fork();
		if (pid < 0) {
			syslog(LOG_ERR, "fork() failed: %s", strerror(errno));
			close(server_fd);
			exit(EXIT_FAILURE);
		}
		if( pid > 0) {
			/*parent process exits */
			exit(EXIT_SUCCESS);
		}

		/*Child becomes the session leader*/
		if( setsid() <0) {
			syslog(LOG_ERR, "setsid() failed: %s", strerror(errno));
			close(server_fd);
			exit(EXIT_FAILURE);
		}
	}

	/* Listen for Incoming connections */
	ret = listen(server_fd, BACKLOG);
	if( ret == -1 ) {
		syslog(LOG_ERR, "listen() failed: %s", strerror(errno));
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	while( !exit_requested ) {
		struct sockaddr_storage client_addr;
		socklen_t client_len = sizeof(client_addr);
		client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
		if( client_fd == -1 ){
			if (errno == EINTR) {
				/* Interrupted by signal, break out to cleanup */
				break;
			}
			syslog( LOG_ERR, "accept() failed: %s", strerror(errno));
			continue;
		}
		
		/*Log accepted connection with client IP */
		char client_ip[INET6_ADDRSTRLEN];
		void *addr;
		
		if (((struct sockaddr*)&client_addr)->sa_family == AF_INET) {
			addr = &(((struct sockaddr_in*)&client_addr)->sin_addr);
		}else {
			addr = &(((struct sockaddr_in6*)&client_addr)->sin6_addr);
		}
		inet_ntop(((struct sockaddr *)&client_addr)->sa_family, addr, client_ip, sizeof(client_ip));
		syslog(LOG_INFO, "Accepted connection from %s", client_ip);
		
		/*Open/Create file to append incoming data*/
		FILE *fp = fopen(DATA_FILE, "a+");
		if( fp == NULL ) {
			syslog(LOG_ERR, "fopen() failed for %s: %s", DATA_FILE, strerror(errno));
			close(client_fd);
			continue;
		}
		
		char recv_buf[BUFFER_SIZE];
		ssize_t bytes_read;
		int newline_received = 0;
		while (( bytes_read = recv(client_fd, recv_buf, BUFFER_SIZE, 0)) > 0) {
			/*Append received data to the file */
			if( fwrite(recv_buf, 1, bytes_read, fp) != (size_t)bytes_read) {
				syslog( LOG_ERR, "fwrite() failed: %s", strerror(errno));
				break;
			}
			/* Check if the received data contains a newline */
			if ( memchr(recv_buf, '\n', bytes_read)) {
				newline_received = 1;
				break;
			}
		}
		
		
		/*Ensure datais flushed to disk */
		fflush(fp);
		
		/*if a newline was found, send the entire file contents back to the client */
		if (newline_received) {
			rewind(fp);
			char file_buf[BUFFER_SIZE];
			size_t bytes_file;
			while ((bytes_file = fread(file_buf, 1, BUFFER_SIZE, fp)) > 0 ) {
				size_t total_sent = 0;
				while (total_sent < bytes_file) {
					ssize_t sent = send(client_fd, file_buf + total_sent, bytes_file - total_sent, 0);
					if ( sent == -1) {
						syslog(LOG_ERR, "send() failed: %s", strerror(errno));
						break;
					}
					total_sent += sent;
				}
			}
		}
		
		fclose(fp);
		close(client_fd);
		client_fd = -1;
		syslog(LOG_INFO, "Close connection from %s", client_ip);
	}
	/* Cleanup: Close the server socket and remove the data file */
	if ( server_fd != -1) {
		close(server_fd);
	}
	unlink(DATA_FILE);
	closelog();
	return 0;
}
			





