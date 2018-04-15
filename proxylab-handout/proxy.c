#include "csapp.h"
#include<errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<string.h>
#include<stdbool.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAXEVENTS 64

void command(void);
int handle_client(int connfd);

//TODO: Make a different event function for each state
int handle_new_client(int listenfd);

void freeEventAction(event_action *);

//One way of thinking about the problem is in terms of "states":
//CLREAD: reading from client, until the entire request has been read from the client.
//SRVWRITE: writing to server, until the entire request has been sent to the server.
//SRVREAD: reading from server, until the entire response has been received.
//CLWRITE: writing to client, until the entire response has been sent to the client.
typedef enum {CLREAD, SRVWRITE, SRVREAD, CLWRITE} serviceState;

struct event_action {
	int (*callback)(int);
	int *clientfd;
    int *serverfd;
    size_t *totalBytesPassed;
    size_t *totalDataSize;
    unsigned char *dataBuf;
    char *URL;
    serviceState *state;
};

int efd;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int main()
{
    int listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
	struct epoll_event *events;
	int i;
	int len;
	int *argptr;
	struct event_action *eventAction;

	size_t n; 
	char buf[MAXLINE]; 

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

    //Create an epoll instance with epoll_create1()
    if ((efd = epoll_create1(0)) < 0) {
		fprintf(stderr, "error creating epoll fd\n");
		exit(EXIT_FAILURE);
	}

    //Set up listen socket.
    listenfd = Open_listenfd(argv[1]);

	// set fd to non-blocking (set flags while keeping existing flags)
	if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option\n");
		exit(EXIT_FAILURE);
	}

    //Set up event action to associate with listenfd
    eventAction = malloc(sizeof(struct event_action));
	eventAction->callback = handle_new_client;
	clientfd = malloc(sizeof(int));
	*clientfd = listenfd;
	eventAction->clientfd = clientfd;
    eventAction->serverfd = NULL;
    eventAction->totalBytesPassed = NULL;
    eventAction->totalDataSize = NULL;
    eventAction->URL = NULL;
    eventAction->serviceState = NULL;

	event.data.ptr = eventAction;
	event.events = EPOLLIN | EPOLLET; // Read mode, use edge-triggered monitoring

    //Register your listen socket with the epoll instance.
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(EXIT_FAILURE);
	}

    //Buffer where events are returned
	events = calloc(MAXEVENTS, sizeof(event));

    //Open your log file.  Since this server is single-threaded, you don't need to use producer-consumer threads, and you can log directly, rather than sending it to a queue (see Client Request Handling below).

    //Initialize your cache.  This time, there is no need to guard read or write access to the cache with semaphores because only a single thread will be accessing the cache.

    //This flag can be used to gracefully end the main loop and free resources before shutdown
    bool runProxy = true;

    //Start an epoll_wait() loop.
    while (runProxy) {
		// wait for event to happen with timeout of 1 second
		n = epoll_wait(efd, events, MAXEVENTS, 1);

        //If the result was a timeout (i.e., return value from epoll_wait() is 0), check if a global flag has been set by a handler and, if so, end the loop; otherwise, continue waiting
        if(!n)
        {
            if(/*check for global flags*/)
            {
                //Free events memory here?
                runProxy = false;
            }
        }
        else if(n < 0) //If the result was an error, handle the error appropriately.
        {

        }
        else //If there was no error, you should loop through all the events and handle each appropriately.
        {
		for (i = 0; i < n; i++) {
			eventAction = (struct event_action *)events[i].data.ptr;
			//argptr = eventAction->arg;
			if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
				/* An error has occured on this fd */
				fprintf(stderr, "epoll error\n");
				//close(*argptr);
				//free(eventAction->arg);
				//free(eventAction);
                freeEventAction(eventAction);
			}

            //rewrite this to handle state transitions
			else if (!eventAction->callback(*argptr)) {
                //free event action struct memory
				close(*argptr);
				free(eventAction->arg);
				free(eventAction);
			}

		}
        }
	}

    //After your epoll_wait() loop, you should clean up any resources (e.g., freeing malloc'd memory), and exit.
	free(events);

    //In your epoll_wait() loop, you will do the following:
    
    

    //Just as with the previous parts of the proxy lab, when you receive an HTTP request from a client, you will first want to check the cache for the resource corresponding to that URL requested.  If it exists, then you can send the resource back to the client.  As mentioned earlier, this time you cannot simply begin sending back over the socket; you must wait until the call to epoll_wait() indicates that the socket is available for writing.
    //If the URL you have requested is not in the cache, then (just as before) your proxy will need to retrieve it from the upstream server.  The difference is that the socket you set up for communication with the server must now be set to non-blocking, just as the listening socket and the client socket are.  And you must register this socket with the epoll instance, for writing, using edge-triggered monitoring.  You can execute connect() immediately, but you cannot initiate the write() call until epoll_wait() indicates that this socket is ready for writing; because the socket is non-blocking, connect() will return before the connection is actually set up.
    //You will need to keep track of the "state" of reach request.  The reason is that, just like when using blocking sockets, you won't always be able to receive or send all your data with a single call to read() or write().  With blocking sockets in a multi-threaded server, the solution was to use a loop that received or sent until you had everything, before you moved on to anything else.  Because it was blocking, the kernel would context switch out the thread and put it into sleep state until there was I/O.  However, with I/O multiplexing and non-blocking I/O, you can't loop until you receive (or send) everything; you have to stop when you get an value less than 0 and finish handling the other ready events, after which you will return to the epoll_wait() loop to see if it is ready for more I/O.  When a return value to read() or write() is less than 0 and errno is EAGAIN or EWOULDBLOCK, it is a an indicator that you are done for the moment--but you need to know where you should start next time it's your turn (see man pages for accept and read, and search for blocking).  For example, you'll need to associate with the request: the file descriptors corresponding to the client socket and the server socket; the request state (see Client Request States); the total number of bytes to read or write for the request or the response; the total number of bytes read or written thus far; the buffer to which you are writing data from a socket or from which you are writing the data to a socket; etc.

    //When you have completed all the I/O (reading or writing) for a given state, there are often items not related to socket I/O that you can perform before you move to the next state.  For example, in the READ_REQUEST state, when you have finished reading the entire request, you can immediately log the request, check the cache, and (if not in the cache), set up to the server socket (including making it non-blocking) and call connect().  After all that, then you can transition to the SEND_REQUEST state (or to the SEND_RESPONSE state, in the case the item was cached).  However, as indicated previously, you may not write until the socket becomes ready for writing--that is, until it is returned by epoll_wait() as being write ready.

    return EXIT_SUCCESS;
}

//If an event corresponds to the listen socket, you should accept() any and all client connections
int handle_new_client(int listenfd) {
	socklen_t clientlen;
	int connfd;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
	int *argptr;
	struct event_action *eventAction;

	clientlen = sizeof(struct sockaddr_storage); 

	// loop and get all the connections that are available
	while ((connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen)) > 0) {

		// set fd to non-blocking (set flags while keeping existing flags)
		if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		eventAction = malloc(sizeof(struct event_action));
        //TODO: change this to state 1 handler after state 1 handler is created.
		eventAction->callback = handle_client;

		clientfd = malloc(sizeof(int));
		*clientfd = connfd;

		eventAction->clientfd = clientfd;

		event.data.ptr = eventAction;
        // add event to epoll file descriptor
		event.events = EPOLLIN | EPOLLET; //Read mode, use edge-triggered monitoring
		if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
			fprintf(stderr, "error adding event\n");
			exit(1);
		}
	}

	if (errno == EWOULDBLOCK || errno == EAGAIN) {
		// no more clients to accept()
		return 1;
	} else {
		perror("error accepting");
		return 0;
	}
}

void freeEventAction(event_action *eventAction)
{
    close(eventAction->clientfd);
    free(eventAction->clientfd);
    if(eventAction->state)
    {
        if(eventAction->serverfd)
        {
            close(eventAction->serverfd);
            free(eventAction->serverfd);
        }

        free(eventAction->totalBytesPassed);
        free(eventAction->totalDataSize);
        free(eventAction->dataBuf);
        free(eventAction->URL);
        free(eventAction->state);
    }

    free(eventAction);
    eventAction = NULL;
}

//If an event corresponds to the socket associated with a client request (client socket) or the socket associated with a the proxied request to an upstream server (server socket) , you should determine where you are in terms of handling the corresponding client request and begin (or resume) handling it.  You should only read() or write() on said socket if your event indicates that you can, and only until the read() or write() call returns a value less than 0.  In such cases (where a value less than 0 is returned), if errno is EAGAIN or EWOULDBLOCK, then that is an indicator that there is no more data to be read, or (for write) that the file descriptor is no longer available for writing.  See the "Client Request Handling" section for more information.