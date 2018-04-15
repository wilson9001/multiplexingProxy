# multiplexingProxy
In this lab, you will again be implementing an HTTP proxy that handles concurrent requests.  However, the server you produce this time will take advantage of I/O multiplexing.  Your server will not spawn any additional threads or processes (i.e., it will be single-threaded), and all sockets will be set to non-blocking.  While your server will not take advantage of multiprocessing, it will be more efficient by holding the processor longer because it is not blocking (and thus sleeping) on I/O.  Popular servers, such as NGINX, use a similar model (see https://www.nginx.com/blog/thread-pools-boost-performance-9x/).  This model is also referred to as an example of event-based programming, wherein execution of code is dependent on "events"--in this case the availability of I/O.

Please read the epoll man page in preparation for this lab.  You will also want to have ready reference to several other man pages related to epoll.  Here is a list:
 
epoll - general overview of epoll, including detailed examples
epoll_create1 - shows the usage of the simple function to create an epoll instance
epoll_ctl - shows the definition of the epoll_data and epoll_event structures, which are used by both epoll_ctl() and epoll_wait().  Also describes the event *types* with which events are registered to an epoll instance, e.g., for reading or writing, and which type of triggering is used (for this lab you will use edge-level triggering).
epoll_wait - shows the usage of the simple epoll_wait() function, including how events are returned and how errors are indicated.

Procedure
The following is a general outline that might help you organize your server code:
 
When you start up your HTTP server:
Create an epoll instance with epoll_create1()
Set up your listen socket (as you've done in previous labs), and configure it to use non-blocking I/O (see the man page for fcntl() for how to do this).
Register your listen socket with the epoll instance that you created, for *reading*.
Open your log file.  Since this server is single-threaded, you don't need to use producer-consumer threads, and you can log directly, rather than sending it to a queue (see Client Request Handling below).
Initialize your cache.  This time, there is no need to guard read or write access to the cache with semaphores because only a single thread will be accessing the cache.
Start an epoll_wait() loop, with a timeout of 1 second.
In your epoll_wait() loop, you will do the following:
If the result was a timeout (i.e., return value from epoll_wait() is 0), check if a global flag has been set by a handler and, if so, break out of the loop; otherwise, continue.
If the result was an error (i.e., return value from epoll_wait() is less than 0), handle the error appropriately (see the man page for epoll_wait for more).
If there was no error, you should loop through all the events and handle each appropriately.  See next bullet items.
If an event corresponds to the listen socket, you should accept() any and all client connections (i.e., in a loop), configure each to use non-blocking I/O (see the man page for fcntl() for how to do this), and register each returned client socket with the epoll instance that you created, for reading, using edge-triggered monitoring.  You will stop calling accept() when it returns a value less than 0.  In such cases, if errno is set to EAGAIN or EWOULDBLOCK, then that is an indicator that there are no more clients currently waiting.
If an event corresponds to the socket associated with a client request (client socket) or the socket associated with a the proxied request to an upstream server (server socket) , you should determine where you are in terms of handling the corresponding client request and begin (or resume) handling it.  You should only read() or write() on said socket if your event indicates that you can, and only until the read() or write() call returns a value less than 0.  In such cases (where a value less than 0 is returned), if errno is EAGAIN or EWOULDBLOCK, then that is an indicator that there is no more data to be read, or (for write) that the file descriptor is no longer available for writing.  See the "Client Request Handling" section for more information.
After your epoll_wait() loop, you should clean up any resources (e.g., freeing malloc'd memory), and exit.
 
Client Request Handling
Just as with the previous parts of the proxy lab, when you receive an HTTP request from a client, you will first want to check the cache for the resource corresponding to that URL requested.  If it exists, then you can send the resource back to the client.  As mentioned earlier, there is no reason to guard access to the cache with semaphores, as there is only a single thread accessing the cache.  However, this time you cannot simply begin sending back over the socket; you must wait until the call to epoll_wait() indicates that the socket is available for writing.
 
If the URL you have requested is not in the cache, then (just as before) your proxy will need to retrieve it from the upstream server.  The difference is that the socket you set up for communication with the server must now be set to non-blocking, just as the listening socket and the client socket are.  And you must register this socket with the epoll instance, for writing, using edge-triggered monitoring.  You can execute connect() immediately, but you cannot initiate the write() call until epoll_wait() indicates that this socket is ready for writing; because the socket is non-blocking, connect() will return before the connection is actually set up.
 
You will need to keep track of the "state" of reach request.  The reason is that, just like when using blocking sockets, you won't always be able to receive or send all your data with a single call to read() or write().  With blocking sockets in a multi-threaded server, the solution was to use a loop that received or sent until you had everything, before you moved on to anything else.  Because it was blocking, the kernel would context switch out the thread and put it into sleep state until there was I/O.  However, with I/O multiplexing and non-blocking I/O, you can't loop until you receive (or send) everything; you have to stop when you get an value less than 0 and finish handling the other ready events, after which you will return to the epoll_wait() loop to see if it is ready for more I/O.  When a return value to read() or write() is less than 0 and errno is EAGAIN or EWOULDBLOCK, it is a an indicator that you are done for the moment--but you need to know where you should start next time it's your turn (see man pages for accept and read, and search for blocking).  For example, you'll need to associate with the request: the file descriptors corresponding to the client socket and the server socket; the request state (see Client Request States); the total number of bytes to read or write for the request or the response; the total number of bytes read or written thus far; the buffer to which you are writing data from a socket or from which you are writing the data to a socket; etc.
 
Client Request States
One way of thinking about the problem is in terms of "states".  The following is an example of a set of client request states, each associated with different I/O operations related to proxy operation:
READ_REQUEST: reading from client, until the entire request has been read from the client.
SEND_REQUEST: writing to server, until the entire request has been sent to the server.
READ_RESPONSE: reading from server, until the entire response has been received.
SEND_RESPONSE: writing to client, until the entire response has been sent to the client.
 
When you have completed all the I/O (reading or writing) for a given state, there are often items not related to socket I/O that you can perform before you move to the next state.  For example, in the READ_REQUEST state, when you have finished reading the entire request, you can immediately log the request, check the cache, and (if not in the cache), set up to the server socket (including making it non-blocking) and call connect().  After all that, then you can transition to the SEND_REQUEST state (or to the SEND_RESPONSE state, in the case the item was cached).  However, as indicated previously, you may not write until the socket becomes ready for writing--that is, until it is returned by epoll_wait() as being write ready.
 
Hints
When planning your assignment, it will greatly help you to create a state machine that includes: all of the above client states; and all the different actions that you should include for handling a client (e.g., read from client, open server connection, log request, etc.).  You will need to know under what conditions a client request will transition from one state to another.
While you might want to use the rio code distributed with with previous proxy labs, please note that it was written for blocking sockets and might not work properly.  If you choose to use it, you will need to modify it.  However, you can also accomplish what you would like using calls to read() and write().
Your code MUST compile and run properly (i.e., as tested by the driver) on the CS lab machines.  Note that you are still welcome to develop in another (Linux) environment (e.g., a virtual machine), but please compile and test on a CS lab machine before submission!
The log entries must include the URL that was requested by the client.  Other than that, the format is loose.
Two notes on getaddrinfo():
While the typical use of getaddrinfo() is to call it and iterate through the linked list of addresses returned to find one that works, that is unnecessary complexity for this lab because of the non-blocking I/O.  You can simply pick the first address in the list.
getaddrinfo() involves performing a DNS lookup, which is, effectively, I/O.  However, there is no asynchronous/non-blocking version of getaddrinfo(), so you may use it in synchronous/blocking fashion (For those interested in a more flexible alternative, see https://getdnsapi.net/).
Read the man pages - really.
 
Reminders
You may not use blocking I/O.  All sockets must be configured as non-blocking.  You need to set this up--it is not the default.  See instructions above.
You may not use threads or multiple processes (i.e., with fork()).
You should not need or want to use semaphores, locks, or other components of synchronization.  You're welcome :)
 
Grading Breakdown
20 pts basic functionality
50 pts concurrency
10 pts cache
10 pts logging
4 pts - code compiles without warnings
3 pts - no bytes "lost" ("definitely", "indirectly", "possibly"), as reported by valgrind
3 pts - no bytes still reachable when program exits, as reported by valgrind
 
Submission
To submit, run "make handin" and upload the resulting tar file to LearningSuite.
 
Early Submission
If your lab is submitted ahead of the due date, you can earn back late penalties for previous assignments turned in late.  For each additional day before the deadline that you submit the lab, one late day will be canceled from a previous assignment of your choosing.  Note that early days do not equate to 10% restored to an assignment--it depends on how many days late the assignment was submitted.  Please see the late policy for more information.  Please note the assignments to which you want the late days reversed in the submission notes.  If you don't have any late submissions, early submissions will not be awarded extra credit.
