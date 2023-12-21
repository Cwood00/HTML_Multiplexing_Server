#define  _POSIX_C_SOURCE 200809L
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/select.h>
#include<assert.h>

#include<sys/socket.h>
#include<arpa/inet.h>

#define exit(N) {fflush(stdout); fflush(stderr); _exit(N); }

#define BUFFER_SIZE 1024
#define MAX_CONNECTIONS 10

//Possible first line of respoce to client requests
const char VALID_RESPONCE[] = "HTTP/1.1 200 OK\r\n";
const char BAD_REQUEST_RESPONCE[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
const char NOT_FOUND_RESPONCE[] = "HTTP/1.1 404 Not Found\r\n\r\n";

struct WrFileData
{
    int connectionFD;
    int requestedFile;
    int bytesRead;
    int fileSize;
};

int rd_pool[MAX_CONNECTIONS];
struct WrFileData wr_pool[MAX_CONNECTIONS];

fd_set rd_set, wr_set;

char requestBuffer[BUFFER_SIZE + 1];
int requestSize = 0;

char requestHeader[BUFFER_SIZE + 1];
int requestHeaderSize = 0;

char requestBody[BUFFER_SIZE + 1];
int requestBodySize = 0;

char headerBuffer[BUFFER_SIZE + 1];
int headerSize = 0;

char bodyBuffer[BUFFER_SIZE + 1];
int bodySize = 0;

char savedData[BUFFER_SIZE + 1];
int savedDataSize = 0;

int requests = 0;
int headerBytes = 0;
int bodyBytes = 0;
int errors = 0;
int errorBytes = 0;

int min(int first, int second)
{
    if(first < second)
        return first;
    return second;
}

static int get_port(void)
{
    int fd = open("port.txt", O_RDONLY);
    if (fd < 0) {
        perror("Could not open port.txt");
        exit(1);
    }

    char buffer[32];
    int r = read(fd, buffer, sizeof(buffer));
    if (r < 0) {
        perror("Could not read port.txt");
        exit(1);
    }

    return atoi(buffer);
}

void setUpRequestBuffers(int connectionFD)
{
    requestBuffer[requestSize] = '\0';
    char* requestHeaderEnd = strstr(requestBuffer, "\r\n\r\n");
    //Easy case of entire request fitting in requestBuffer
    if(requestSize < BUFFER_SIZE)
    {
        if(requestHeaderEnd != NULL)
        {
            *requestHeaderEnd = '\0';
            requestHeaderSize = sprintf(requestHeader, "%s", requestBuffer);
            requestBodySize = sprintf(requestBody, "%s", requestHeaderEnd + 4);
        }
        else
        {
            requestHeader[0] = '\0';
            requestBody[0] = '\0';
        }
    }
    //hard case of entire request not fitting in request buffer
    else
    {
        //Entire header fits in requestBuffer
        if(requestHeaderEnd != NULL)
        {
            *requestHeaderEnd = '\0';
            requestHeaderSize = sprintf(requestHeader, "%s", requestBuffer);
            int readBodyBytes = sprintf(requestBody, "%s", requestHeaderEnd + 4);
            int remainingBodyBytes = recv(connectionFD, requestBuffer, BUFFER_SIZE - readBodyBytes, 0);
            snprintf(requestBody + readBodyBytes, (BUFFER_SIZE - readBodyBytes) + 1, "%s", requestBuffer);
            requestBodySize = strlen(requestBody);
        }
        else
        {
            requestHeaderSize = sprintf(requestHeader, "%s", requestBuffer);
            requestBody[0] = '\0';
            //Find start of body
            /*
            while(requestHeaderEnd == NULL)
            {
                recv(connectionFD, requestBuffer, BUFFER_SIZE, 0);
                requestHeaderEnd = strstr(requestBuffer, "\r\n\r\n");
            }
            */
        }

    }
}

void sendResponce(int connectionFD)
{
    int responceLineLength = strlen(VALID_RESPONCE);
    send(connectionFD, VALID_RESPONCE, responceLineLength, 0);
    send(connectionFD, headerBuffer, headerSize, 0);
    send(connectionFD, bodyBuffer, bodySize, 0);

    requests += 1;
    headerBytes += responceLineLength + headerSize;
    bodyBytes += bodySize;
}

void handlePing(int connectionFD)
{
    sprintf(headerBuffer, "Content-Length: 4\r\n\r\n");
    headerSize = strlen(headerBuffer);
    sprintf(bodyBuffer, "pong");
    bodySize = strlen(bodyBuffer);
    sendResponce(connectionFD);
}

void handleEcho(int connectionFD)
{
    char* requestHeaderStart = strstr(requestHeader, "\r\n") + 2;
    bodySize = strlen(requestHeaderStart);
    sprintf(headerBuffer, "Content-Length: %d\r\n\r\n", bodySize);
    headerSize = strlen(headerBuffer);
    sprintf(bodyBuffer, "%s", requestHeaderStart);
    sendResponce(connectionFD);
}

void handleRead(int connectionFD)
{
    if(savedDataSize == 0)
    {
        sprintf(headerBuffer, "Content-Length: 7\r\n\r\n");
        headerSize = 21;
        sprintf(bodyBuffer, "<empty>");
        bodySize = 7;
    }
    else
    {
        sprintf(headerBuffer, "Content-Length: %d\r\n\r\n", savedDataSize);
        headerSize = strlen(headerBuffer);
        snprintf(bodyBuffer, savedDataSize + 1, "%s", savedData);
        bodySize = savedDataSize;
    }
    sendResponce(connectionFD);
}

void handlePost(int connectionFD)
{
    int contentLength = atoi(strstr(requestHeader, "Content-Length: ") + 16);
    savedDataSize = min(contentLength, BUFFER_SIZE);
    snprintf(savedData, savedDataSize + 1,"%s", requestBody);
    handleRead(connectionFD);
}

void handelStats(int connectionFD)
{
    sprintf(bodyBuffer, "Requests: %d\nHeader bytes: %d\nBody bytes: %d\nErrors: %d\nError bytes: %d",
                        requests, headerBytes, bodyBytes, errors, errorBytes);
    bodySize = strlen(bodyBuffer);
    sprintf(headerBuffer, "Content-Length: %d\r\n\r\n", bodySize);
    headerSize = strlen(headerBuffer);

    sendResponce(connectionFD);
}

void send1kBytesFromFile(struct WrFileData *fileData)
{
    int newBytesRead = read(fileData->requestedFile, bodyBuffer, BUFFER_SIZE);
    fileData->bytesRead += newBytesRead;
    send(fileData->connectionFD, bodyBuffer, newBytesRead, 0);

    bodyBytes += newBytesRead;
    if(fileData->bytesRead >= fileData->fileSize)
    {
        sleep(0.1);

        FD_CLR(fileData->connectionFD, &wr_set);
        close(fileData->requestedFile);
        close(fileData->connectionFD);
        for(int i = 0; i < MAX_CONNECTIONS; i++)
        {
            if(wr_pool[i].connectionFD == fileData->connectionFD)
            {
                wr_pool[i].requestedFile = -1;                
                wr_pool[i].connectionFD = -1;
                break;
            }
        }
    }
}

void handleDefaultGet(int connectionFD)
{
    char* endOfURL = strstr(requestHeader, " HTTP/1.1");
    *endOfURL = '\0';
    char* fileName = strstr(requestHeader, "/") + 1;

    int requestedFile = open(fileName, O_RDONLY, 0);

    //File cannot be found
    if(requestedFile == -1)
    {
        int errorLength = strlen(NOT_FOUND_RESPONCE);
        send(connectionFD, NOT_FOUND_RESPONCE, errorLength, 0);

        errors += 1;
        errorBytes += errorLength;
    }
    //File has been found
    else
    {
        struct stat fileInfo;

        stat(fileName, &fileInfo);

        int fileSize = fileInfo.st_size;

        int bytesRead = read(requestedFile, bodyBuffer, BUFFER_SIZE);
        bodySize = bytesRead;
        
        sprintf(headerBuffer, "Content-Length: %d\r\n\r\n", fileSize);
        headerSize = strlen(headerBuffer);
        sendResponce(connectionFD);

        if(bytesRead < fileSize)
        {
            FD_SET(connectionFD, &wr_set);
            for(int i = 0; i < MAX_CONNECTIONS; i++)
            {
                if(wr_pool[i].connectionFD == -1)
                {
                    wr_pool[i].connectionFD = connectionFD;
                    wr_pool[i].requestedFile = requestedFile;
                    wr_pool[i].bytesRead = bytesRead;
                    wr_pool[i].fileSize = fileSize;
                    break;
                }
            }
        }
        else
        {
            close(requestedFile);
        }
    }
}

void handleRequest(int connectionFD)
{
    requestSize = recv(connectionFD, requestBuffer, BUFFER_SIZE, 0);

    setUpRequestBuffers(connectionFD);
    
    if(!strncmp(requestHeader, "GET /ping", 9))
    {
        handlePing(connectionFD);
    }
    else if(!strncmp(requestHeader, "GET /echo", 9))
    {
        handleEcho(connectionFD);
    }
    else if(!strncmp(requestHeader, "POST /write", 11))
    {
        handlePost(connectionFD);
    }
    else if(!strncmp(requestHeader, "GET /read", 9))
    {
        handleRead(connectionFD);
    }
    else if(!strncmp(requestHeader, "GET /stats", 10))
    {
        handelStats(connectionFD);
    }
    else if(!strncmp(requestHeader, "GET /", 5))
    {
        handleDefaultGet(connectionFD);
    }
    else if(requestHeader[0])
    {
        int errorLength = strlen(BAD_REQUEST_RESPONCE);
        send(connectionFD, BAD_REQUEST_RESPONCE, errorLength, 0);

        errors += 1;
        errorBytes += errorLength;
    }
    sleep(0.1);
    FD_CLR(connectionFD, &rd_set);

    for(int i = 0; i < MAX_CONNECTIONS; i++)
    {
        if(rd_pool[i] == connectionFD)
        {
            rd_pool[i] = -1;
        }
    }

    if(!FD_ISSET(connectionFD, &wr_set))
        close(connectionFD);
}

int main(int argc, char** argv)
{
    int port = get_port();

    printf("Using port %d\n", port);
    printf("PID: %d\n", getpid());

    int serverFD = socket(AF_INET, SOCK_STREAM, 0);
    int optVal = 1;
    setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &(server.sin_addr));

    bind(serverFD, (struct sockaddr *)&server, sizeof(server));
    listen(serverFD, MAX_CONNECTIONS);

    struct sockaddr connection;
    unsigned int connectionLength = sizeof(connection);

    //Set up fd_sets
    FD_ZERO(&rd_set);
    FD_ZERO(&wr_set);
    FD_SET(serverFD, &rd_set);

    for(int i = 0; i < MAX_CONNECTIONS; i++)
    {
        wr_pool[i].connectionFD = -1;
        wr_pool[i].requestedFile = -1;
        rd_pool[i] = -1;
    }

    // Process client requests
    while (1) {
        fd_set readReadySet = rd_set;
        fd_set writeReadySet = wr_set;

        select(FD_SETSIZE, &readReadySet, &writeReadySet, NULL, NULL);

        int connectionFD;

        if(FD_ISSET(serverFD, &readReadySet))
        {
            connectionFD = accept(serverFD, (struct sockaddr *)&server, &connectionLength);
            FD_SET(connectionFD, &rd_set);
            for(int i = 0; i < MAX_CONNECTIONS; i++)
            {
                if(rd_pool[i] == -1)
                {
                    rd_pool[i] = connectionFD;
                    break;
                }
            }
        }
        for(int i = 0; i < MAX_CONNECTIONS; i++)
        {
            if(FD_ISSET(rd_pool[i], &readReadySet))
                handleRequest(connectionFD);
        }
        for(int i = 0; i < MAX_CONNECTIONS; i++)
        {
            if(FD_ISSET(wr_pool[i].connectionFD, &writeReadySet))
                send1kBytesFromFile(&(wr_pool[i]));
        }
    }

    return 0;
}
