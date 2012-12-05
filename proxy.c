//
#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"
#include "cache2.c"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define DEBUG(X) //X
#define DEBUG1(X) //X


// global variables
Cache cache;
static const char *user_agentStr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *acceptStr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encodingStr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connectionStr = "Connection: close\r\n";
static const char *proxyConnectionStr = "Proxy-Connection: close\r\n";
int numThreads = 0;
int maxThreads = 16;

//function prototypes
void handleRequest(int *toClientFDPtr);
char *correctHeaders(rio_t *rp, char *buf, char *host, char *portStr);
void clienterror(int fd, char *cause, char *errnum, 
				char *shortmsg, char *longmsg);

void sigintHandler(int sig) {
	printf("freeing cache\n");
	printf("stats: numRuns: %d\n", numThreads);
	freeCache(cache);
	exit(0);
}
				
int main(int argc, char **argv) {
	int listenfd, *connfdPtr;
	unsigned int clientlen;
    struct sockaddr_in clientaddr;
	pthread_t tid;
	
	if(argc != 2) {
		printf("bad params\n");
		exit(0);
	}
	
	Signal(SIGPIPE, SIG_IGN);
	Signal(SIGTSTP, sigintHandler);
	cache = newCache(10, MAX_OBJECT_SIZE, MAX_CACHE_SIZE);
	
    listenfd = Open_listenfd(atoi(argv[1]));
    while (1) {
		clientlen = sizeof(clientaddr);
		connfdPtr = Malloc(sizeof(int));
		*connfdPtr = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		DEBUG(printf("connection found...\n");)
		//while(numThreads >= maxThreads) {}
		Pthread_create(&tid, NULL, handleRequest, connfdPtr);
		numThreads++;
		//handleRequest(connfdPtr);
    }
	
	//freeCache(cache);
	/*void *ret;
	Pthread_exit(ret);*/
	return 0;
}

void handleRequest(int *toClientFDPtr) {
	Pthread_detach(Pthread_self());
	int toClientFD = *toClientFDPtr;
	free(toClientFDPtr); //we can free early since it just stores a primative
	
	int toServerFD;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], 
			version[MAXLINE], hostname[MAXLINE], portStr[6]; //max port length is 5 characters
	hostname[0] = '\0'; portStr[0] = '\0';
    rio_t clientRIO, serverRIO;
	char *request;
	int responseAllocLen = MAX_OBJECT_SIZE-1;
	int responseLen = 0;
	char *response = Malloc(responseAllocLen);
	void *response2 = response+1;
	int cacheResponse = 1; //bool weither or not to cache the response

	
  
    /* Read request line and headers */
    Rio_readinitb(&clientRIO, toClientFD);
    Rio_readlineb(&clientRIO, buf, MAXLINE); //read the get request
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) { 
        clienterror(toClientFD, method, "501", "Not Implemented",
                "Proxy does not implement this method");
        return;
    }
	
    request = correctHeaders(&clientRIO, buf, hostname, (char *) portStr); //remember to free later
	DEBUG(printf("host: %s| port: %s\n", hostname, portStr);)
	DEBUG(printf("correctedHeader:\n%s", request);)
	
	//try to find request in cache
	if((responseLen = readCache(cache, request, (void **) &response2)) > 0) {
		DEBUG1(printf("----use cache---\n");)
		DEBUG(printf("found in cache: \n");)
		DEBUG(printf("%s\n", response);)
		
		response[0] = 'H';
		
		/*int readDiff = memcmp(response, cachedResponse, cachedResponseLen);
		printf("readDiff: %d\n", readDiff);*/
		Rio_writen(toClientFD, response, responseLen); //we will need a response length field
		cacheResponse = 0;
		//exit(0);
	}
	else {
		DEBUG1(printf("----not using cache---\n");)
		
		toServerFD = Open_clientfd(hostname, portStr);
		
		// read from server and write to client only if valid connection to server
		if(toServerFD >= 0) {
			Rio_readinitb(&serverRIO, toServerFD);
			
			//send request
			Rio_writen(toServerFD, request, strlen(request));
			
			//get response and immediatly write it to the client
			int bufLen;
			while((bufLen = Rio_readnb(&serverRIO, buf, MAXLINE)) > 0) {
				Rio_writen(toClientFD, buf, bufLen);
				//write to responseBuf for caching later, and if no space left, do not cache
				if(cacheResponse != 0) {
					int newResponseLen = responseLen+bufLen;
					if(newResponseLen <= responseAllocLen) {
						DEBUG(printf("copying: %s, bufLen: %d to response(%x)+responseLen(%x) = %x\n", buf, bufLen, response, responseLen, response+responseLen);)
						void *resWritePos = (response+responseLen);
						memcpy((char *)resWritePos, buf, bufLen);
						responseLen = newResponseLen;
						DEBUG(printf("copied: %s\n", response);)		
					}
					else {
						DEBUG1(printf("data to large\n");)
						cacheResponse = 0;
					}
				}
			}
			Close(toServerFD);
			
			if(cacheResponse != 0) {
				writeCache(cache, request, (void *) response, responseLen);
				DEBUG1(printf("-----wrote into cache--------\n");)
			}
		}
	}
	
	DEBUG(printf("freeing responsePtr: %x,\n %s\n", response, response);)
	DEBUG(if(response == NULL) {printf("true\n"); exit(0);})
	free(response);
	
	free(request);
	Close(toClientFD);
	DEBUG(printf("connection closed\n\n");)
	//numThreads--;
	return;
}

char *correctHeaders(rio_t *rp, char *buf, char *host, char *portStr) {
	int resultActLen = strlen(buf)+strlen(acceptStr)+strlen(accept_encodingStr)
			+strlen(user_agentStr)+strlen(proxyConnectionStr)
			+strlen(connectionStr)+2; //+2 for /r/n
	int resultAllocLen = 0;
	char *result = (char *) Malloc(sizeof(char)*(resultActLen+1)); //+1 for null term
	result[resultAllocLen] = '\0';
	
	strcpy(result, buf); //buf is the get request line
	strcat(result, user_agentStr);
	strcat(result, acceptStr);
	strcat(result, accept_encodingStr);
	strcat(result, proxyConnectionStr);
	strcat(result, connectionStr);
	resultAllocLen = resultActLen-2;
	
    while(strcmp(buf, "\r\n") && strcmp(buf, "\n")) {
		char key[MAXLINE], value[MAXLINE], *append;
		
		Rio_readlineb(rp, buf, MAXLINE);
		sscanf(buf, "%s %s", key, value);
		append = "";
		/*if(!strcasecmp(key, "cookie:") || !strcasecmp(key, "proxy-connection:") 
			|| !strcasecmp(key, "connection:") || !strcasecmp(key, "accept-encoding:")
			|| !strcasecmp(key, "accept:") || !strcasecmp(key, "user-agent:")) 
		{append = "";}*/
		if(!strcasecmp(key , "host:") || !strcasecmp(key, "accept-language:")){
			append = buf;
			if(!strcasecmp(key, "host:")) {
				char *portStrTmp, *hostTmp;
				hostTmp = strtok_r(value, ":", &portStrTmp);
				if(strlen(hostTmp) == 0) {strcpy(host, value);}
				else {strcpy(host, hostTmp);}
				if(strlen(portStrTmp) == 0) {strcpy(portStr, "80");}
				else {strcpy(portStr, portStrTmp);}
			}
		}
		
		int sizeNeeded = resultAllocLen + strlen(append);
		if(sizeNeeded >= resultActLen) {
			int resizeSize = 2*resultAllocLen;
			if(sizeNeeded > resizeSize) {resizeSize = sizeNeeded;}
			result = (char *) Realloc(result, sizeof(char)*(resizeSize+1)); //+1 for null terminator
			resultActLen = resizeSize;
		}
		
		strcat(result, append);
		resultAllocLen = sizeNeeded;
    }
	
	//end request
	
	if(resultAllocLen+2 < resultActLen) {//downsize if necessary, +2 because we are saving space of '\r\n'
		result = (char *) Realloc(result, sizeof(char)*(resultAllocLen+3)); //+3 for null terminator and EOF flag ('\r\n')
	}
	result[resultAllocLen] = '\r'; result[resultAllocLen+1] = '\n'; result[resultAllocLen+2] = '\0';
	
    return result;
}


//from tiny.c
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}


