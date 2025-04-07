/*
 * aesdsocket.c - Multithread socket server with timestamp timer.
 *
 * This version creates a new thread for each incoming connection, 
 * Synchronizes writes to /var/tmp/aesdsocketdata using a mutex,
 * and spawns a timer thread to append a timestamp every 10 seconds.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>


#define PORT "9000"
#define BACKLOG 10	
#define BUFFER_SIZE  1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t exit_requested = 0;
int server_fd = -1;

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;		// protects file writes
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;  // protects thread list


/*Structure for passing connection parameters to thread */
typedef struct thread_arg {
	int client_fd;
	char client_ip[INET6_ADDRSTRLEN];
} thread_arg_t;

/*Singly linked list node for connection threads */
typedef struct thread_node {
	pthread_t thread_id;
	LIST_ENTRY(thread_node) pointers;
} thread_node_t;

/*Declare the head for our thread linked list*/
LIST_HEAD(thread_list, thread_node);
struct thread_list thread_list_head;	//Global list head


/*Signal Handler to set exit flag */
void signal_handler ( int signum )
{
	syslog(LOG_INFO, "Caught signal, exiting");
	exit_requested = 1;
}

/*Add a thread node to the linked list*/
void add_thread_node(pthread_t thread_id)
{
	thread_node_t *node = malloc(sizeof(thread_node_t));
	if (!node) {
		syslog(LOG_ERR, "malloc() failed for thread_node");
		return ;
	}
	node->thread_id = thread_id;

	pthread_mutex_lock(&thread_list_mutex);
	LIST_INSERT_HEAD(&thread_list_head, node, pointers);
	pthread_mutex_unlock(&thread_list_mutex);
}

void write_timestamp(void)
{
	time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char tim_str[128];
        /*Format timestamp in RFC2822 style (e.g. "Fri, 21 Nov 1997 09:55:06 -0600" */
         strftime(tim_str, sizeof(tim_str), "%a, %d %b %Y %T %z", tm_info);

        char timestamp_str[256];
        snprintf(timestamp_str, sizeof(timestamp_str), "timestamp:%s\n",tim_str);
        pthread_mutex_lock(&file_mutex);
        FILE *fp = fopen(DATA_FILE, "a");
        if ( fp != NULL ) {
               fwrite(timestamp_str, 1, strlen(timestamp_str), fp);
               fflush(fp);
               fclose(fp);
        } else {
                syslog(LOG_ERR, "fopen() failed for timestamp write: %s", strerror(errno));
        }
        pthread_mutex_unlock(&file_mutex);
}


/*Timer thread Function: append timestamp every 10 seconds */

void *timer_thread_func (void *arg)
{
	(void)arg; //unused
	write_timestamp();

	while( !exit_requested ) {
		sleep(10);
		if(exit_requested)
			break;
		write_timestamp();
	}
	return NULL;
}

/* Thread function for handling a client connection */
void *connection_handler (void *arg) 
{
	thread_arg_t *targ = (thread_arg_t*)arg;
	int client_fd = targ->client_fd;
	char client_ip[INET6_ADDRSTRLEN];
	strncpy(client_ip, targ->client_ip, sizeof(client_ip));
	free(targ);

	syslog(LOG_INFO, "Accepted connection from %s", client_ip);

	char recv_buf[BUFFER_SIZE];
	ssize_t bytes_read;
	int newline_received = 0;

	/*Read from client and write to file*/
	while((bytes_read = recv(client_fd, recv_buf, BUFFER_SIZE, 0)) > 0) {
		pthread_mutex_lock(&file_mutex);
		FILE *fp = fopen(DATA_FILE, "a+");
		if (fp == NULL) {
			syslog(LOG_ERR, "fopen() failed for %s %s",DATA_FILE, strerror(errno));
			pthread_mutex_unlock(&file_mutex);
			break;
		}

		if( fwrite(recv_buf, 1, bytes_read, fp) != (size_t)bytes_read) {
			syslog(LOG_ERR, "fwrite() failed: %s", strerror(errno));
			fclose(fp);
			pthread_mutex_unlock(&file_mutex);
			break;
		}
		fflush(fp);
		/* Check if received data contains a newline */
		if( memchr(recv_buf, '\n', bytes_read) != NULL )
			newline_received = 1;
		fclose(fp);
		pthread_mutex_unlock(&file_mutex);

		if( newline_received )
			break;
	}

	/* if newline received send back the entire file */
	if (newline_received ) {
		pthread_mutex_lock(&file_mutex);
		FILE *fp = fopen(DATA_FILE, "r");
		if (fp != NULL) {
			char file_buf[BUFFER_SIZE];
			size_t bytes_file;
			while(( bytes_file = fread(file_buf, 1, BUFFER_SIZE, fp)) > 0) {
				size_t total_sent = 0;
				while (total_sent < bytes_file ) {
					ssize_t sent = send(client_fd, file_buf + total_sent, bytes_file - total_sent, 0);
					if (sent == -1){
						syslog(LOG_ERR, "send() failed: %s", strerror(errno));
						break;
					}
					total_sent += sent;
				}
			}
			fclose(fp);
		}else {
			syslog(LOG_ERR, "fopen() failed for reading %s: %s", DATA_FILE, strerror(errno));
		}
		pthread_mutex_unlock(&file_mutex);
	}

	close(client_fd);
	syslog(LOG_INFO, "Closed connection from %s", client_ip);
	return NULL;
}

int main (int argc, char *argv[])
{
	int daemon_mode = 0;
	int ret;

	/*Process command line arguments */
	int opt;
	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch(opt) {
			case 'd':
				daemon_mode = 1;
				break;
			default:
				fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	/*open syslog for logging*/
	openlog("aesdsocket", LOG_PID, LOG_USER);

	/*Initializes Thread list */
	LIST_INIT(&thread_list_head);

	/*setup signal handling for SIGINT and SIGTERM*/
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* Use getaddrinfo() to obtain address(es) for binding */
	struct addrinfo hints;
	struct addrinfo *res, *rp;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; 	/* Use IPv4 */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; 	/* Use wildcard IP address */

	ret = getaddrinfo(NULL, PORT, &hints, &res);
	if( ret != 0) {
		syslog(LOG_ERR, "getaddrinfo() failed: %s",gai_strerror(ret));
		exit(EXIT_FAILURE);
	}

	/* Loop through all the results and bind to the first we can */
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		server_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if( server_fd == -1)
			continue;
		int yes = 1;
		if( setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
			syslog(LOG_ERR, "setsockopt() failed: %s", strerror(errno));
			close(server_fd);
			continue;
		}

		if( bind(server_fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(server_fd);
	}
	freeaddrinfo(res);

	if( rp == NULL )
	{
		syslog(LOG_ERR, "Could not bind to any address");
		exit(EXIT_FAILURE);
	}

	/* If daemon mode is requested, fork after binding*/
	if( daemon_mode ) {
		pid_t pid = fork();
		if(pid < 0) {
			syslog(LOG_ERR, "fork() failed: %s", strerror(errno));
			close(server_fd);
			exit(EXIT_FAILURE);
		}
		if(pid > 0) {
			/* Parent process exits */
			exit(EXIT_SUCCESS);
		}
		/*Child becomes the session leader */
		if( setsid() < 0) {
			syslog(LOG_ERR, "setsid() failed: %s", strerror(errno));
			close(server_fd);
			exit(EXIT_FAILURE);
		}
	}

	/* Listen for incoming connections */
	ret = listen( server_fd, BACKLOG);
	if ( ret == -1 ){
		syslog(LOG_ERR, "listen() failed: %s", strerror(errno));
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	/*Create the timer thread*/
	pthread_t timer_thread;
	if( pthread_create(&timer_thread, NULL, timer_thread_func, NULL) != 0) {
		syslog(LOG_ERR, "pthread_create for timer thread failed: %s", strerror(errno));
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	/* Main accept loop */
	while( !exit_requested ) {
		struct sockaddr_storage client_addr;
		socklen_t client_len = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
		if( client_fd == -1 )
		{
			if( errno == EINTR ) {
				/* Interrupted by signal, break out to cleanup */
				break;
			}
			syslog(LOG_ERR, "accept() failed: %s", strerror(errno));
			continue;
		}

		/* Get Client IP address */
		char client_ip[INET6_ADDRSTRLEN];
		void *addr;
		if (((struct sockaddr*)&client_addr)->sa_family == AF_INET)
			addr = &(((struct sockaddr_in*)&client_addr)->sin_addr);
		else
			addr = &(((struct sockaddr_in6*)&client_addr)->sin6_addr);

		inet_ntop(((struct sockaddr*)&client_addr)->sa_family, addr, client_ip, sizeof(client_ip));

		/*Allocate and populate thread argument*/
		thread_arg_t *targ = malloc(sizeof(thread_arg_t));
		if( !targ ) {
			syslog(LOG_ERR, "malloc() failed for thread argument");
			close(client_fd);
			continue;
		}

		targ->client_fd = client_fd;
		strncpy(targ->client_ip, client_ip, sizeof(targ->client_ip));

		/*create a new thread for this connection*/
		pthread_t thread_id;
		if (pthread_create(&thread_id, NULL, connection_handler, targ) != 0) {
			syslog(LOG_ERR, "pthread_create() failed: %s", strerror(errno));
			free(targ);
			close(client_fd);
			continue;
		}
		/*Add thread info to our linked list using sys/queue.h */
		add_thread_node(thread_id);
	}

	/*Cleanup: Stop accepting new connections, close server socket */
	if( server_fd != -1) {
		close(server_fd);
	}

	/*Wait for the thread timer to finish */
	pthread_join(timer_thread, NULL);

	/* Join all connection threads using the queue.h list */
	pthread_mutex_lock(&thread_list_mutex);
	thread_node_t *node;
	while( !LIST_EMPTY(&thread_list_head)) {
		node = LIST_FIRST(&thread_list_head);
		pthread_join(node->thread_id, NULL);
		LIST_REMOVE(node, pointers);
		free(node);
	}

	pthread_mutex_unlock(&thread_list_mutex);

	/*Remove Data file*/
	unlink(DATA_FILE);
	
	closelog();

	return 0;
}







