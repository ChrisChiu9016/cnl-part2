// 資料處理相關
#include <iostream>
#include <stdlib.h> // for strtol
#include <errno.h>  // for errno
#include <limits.h> // for INT_MAX, INT_MIN
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <regex>
#include <sys/stat.h>
#include <fcntl.h>
// 網路相關
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

class Target{
public:
    Target(){};
    char  method[512];
    char  filename[512];
    char  version[512];
    bool  hasRange;
    int   size;
    int   start;
};

int PORT = 9527;

void BadRequest(int conn){
    char szRes[] = "HTTP/1.0 400 BAD REQUEST\nContent-Type: text/html\n\n<!DOCTYPE html><html><head><title>400 BAD REQUEST</title></head><body><h1>400 BAD REQUEST</h1></body></html>";
    if (send(conn, szRes, strlen(szRes), 0) == -1)
    {
        cerr << "write error.\n";
        exit(1);
    }
    cout << "Bad request sended.back.\n";
}

void parseRequest(int conn, char* szReq, Target& req){
    // setup struct for response
    string METHOD = "", targetFile = "", version = "";
    char szRes[4096];

    // get mothod, filename and protocol
    char *request;

    // METHOD
    request = strtok(szReq, " ");
    METHOD += request;

    // filename
    request = strtok(NULL, " ");
    if(request == NULL){
        cout << "nullptr on filename\nsend status code 400 BAD REQUEST." << endl;
        BadRequest(conn);
    }
    request++;  // ingore the first element '/'
    targetFile += request;

    // version
    request = strtok(NULL, " ");
    if(request == NULL){
        cout << "nullptr on version\nsend status code 400 BAD REQUEST." << endl;
        BadRequest(conn);
    }
    request = strtok(request, "\r\n");
    version += request;

    // set parameeter
    strncpy(req.method, METHOD.c_str(), 20);
    strncpy(req.filename, targetFile.c_str(), 200);
    strncpy(req.version, version.c_str(), 20);

    smatch range;
    string input = szReq;
    regex reg("Range: ([A-Za-z]*)=([0-9]*)-([0-9]*)");
    if(regex_match(input, range, reg)){
        if(range.str(1) != "bytes"){
            BadRequest(conn);
        }
        else{
            req.hasRange = true;
            char *p;
            req.start = stoi(range.str(2));
            req.size = stoi(range.str(3)) - req.start;
        }
    }
    else{
        req.hasRange = false;
        req.size = 0;
        req.start = 0;
    }
}

int handleRequest(int conn){
    char szReq[4096]{'\0'} ,szRes[4096]{'\0'};
    while(1){
        memset(szReq, 0, sizeof(szReq));

        int ret = read(conn, szReq, sizeof(szReq));
        if(ret == 0){
            // 客戶端關閉
            printf("client closed.\n");
            break;
        }
        else if(ret == -1){
            cerr << "read error.\n";
            exit(1);
        }
        // parse request
        fputs(szReq, stdout);
        Target req;
        parseRequest(conn, szReq, req);
        cout << req.filename << endl;
        int fd = open(req.filename, O_RDONLY);
        if(req.method != "GET" || req.version != "HTTP/1.1"){
            BadRequest(conn);
        }
        else if(fd < 0){
            printf("Open file %s successfully.\n", req.filename);            
        }
        // send response
    }
}

int main(int argc, char* argv[]){
    // get PORT number from argv[]
    char *p;
    long arg = strtol(argv[1], &p, 10);
    if (errno != 0 || *p != '\0' || arg > INT_MAX || arg < INT_MIN)
    {
        cout << "Invalid PORT number.\n";
        return -1;
    }
    else
    {
        // No error
        // check if port is valid
        if (arg < 0 || arg > 65535)
        {
            cerr << "Port number out of range.\n";
            return -1;
        }
        else if (arg >= 0 && arg <= 1023 && arg != 80)
        {
            cerr << "Privileged services port number.\n";
            return -1;
        }
        else
        {
            PORT = arg;
            printf("Server listening on port %d\n\n", PORT);
        }
    }

    // create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0); //change socket to int
    if (sock == -1)
        return -1;

    // bind the ip address to the socket
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_port = htons(PORT);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;

    if(bind(sock, (struct sockaddr*)&sin, sizeof(sin))<0){
        cerr << "bind error.\n";
    }
    
    // tell winsock the socket is for listening
    if(listen(sock, SOMAXCONN)<0){
        cerr << "listen error.\n";
    }

    // while loop
    char szBuf[4096]{'\0'};

    // wait for a connection
    sockaddr_in client;
    socklen_t clientsize = sizeof(client);
    int conn;

    pid_t id;

    while(1){
        // create new socket for client
        conn = accept(sock, (sockaddr *)&client, &clientsize);  // 三次握手
        if (conn == -1)
            return -1;
        printf("recv connect ip=%s port=%d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        
        // do pid_t = fork() here;
        id = fork();

        if(id == -1){
            cout << "fork error.\n";
            return -1;
        }
        if(id == 0){   // 子程序
            close(sock);
            handleRequest(conn);
            exit(0);
        }
        else if(id > 0){
            close(conn);
        }
    }
    return 0;
}

/*            ---code reference---           *
https://www.kshuang.xyz/doku.php/programming:c:socket
https://bbs.csdn.net/topics/370091221          
https://www.itread01.com/content/1547180673.html
https://www.itread01.com/content/1548101702.html
https://www.itread01.com/p/168113.html
https://stackoverflow.com/questions/9197689/invalid-conversion-from-int-to-socklen
*/