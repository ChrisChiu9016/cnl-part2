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
    int   start;
};

int PORT = 9527;

void BadRequest(int conn){
    char szRes[] = "HTTP/1.0 400 BAD REQUEST\nContent-Type: text/html\n\n";
    send(conn, szRes, strlen(szRes), MSG_NOSIGNAL);
    
    char body[] = "<!DOCTYPE html><html><head><title>400 BAD REQUEST</title></head><body><h1>400 BAD REQUEST</h1></body></html>";
    send(conn, body, strlen(body), 0);
    cout << "Bad request sended back.\n";
    exit(1);
}
void NotFound(int conn){
    char szRes[] = "HTTP/1.0 404 NOT FOUND\nContent-Type: text/html\n\n";
    send(conn, szRes, strlen(szRes), 0);
    
    char body[] = "<!DOCTYPE html><html><head><title>404 NOT FOUND</title></head><body><h1>404 NOT FOUND</h1></body></html>";
    send(conn, body, strlen(body), MSG_NOSIGNAL);

    cout << "404 NOT FOUND sended back.\n";
    exit(1);
}


void parseRequest(int conn, char* szReq, Target& req){
    // setup struct for response
    string METHOD = "", targetFile = "", version = "";

    // check range in request
    char* ptr;
    if((ptr = strstr(szReq, "Range: bytes=")) == NULL){
        cout << "no range in request.\n";
        req.hasRange = false;
        req.start = 0;
    }
    else{
        cout << "find range in request.\n";
        ptr = ptr + 13;
        ptr = strtok(ptr, "-");
        req.hasRange = true;
        string start = ptr;
        req.start = stoi(ptr);
        cout << "start at: " << req.start << endl;
    }

    // METHOD
    char* request = strtok(szReq, " ");
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
}

void handleRequest(int conn){
    char szReq[4096]{'\0'};
    while(1){
        memset(szReq, 0, sizeof(szReq));

        int ret = read(conn, szReq, sizeof(szReq));
        if(ret == 0){
            // 客戶端關閉
            printf("client closed.\n\n");
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
        if(strcmp(req.method, "GET") || strcmp(req.version, "HTTP/1.1")){
            cerr << "invalid method or http version.\n";
            BadRequest(conn);
        }

        // 判斷出正確的Content-Type
        char type[20];
        if(strstr(req.filename, ".mp4") != NULL){
            strcpy(type, "video/mp4");
        }
        else if(strstr(req.filename, ".html") != NULL){
            strcpy(type, "text/html");
        }

        // 判斷是否需要Range Request
        char szRes[4096]{'\0'};
        memset(szRes, 0, 4096);
        if(strcmp(type, "video/mp4") == 0){
            // 讀檔
            FILE *fd = fopen(req.filename, "rb+");
            if(fd == NULL){
                printf("%s file does not exist.\n", req.filename); 
                NotFound(conn);
                exit(1);     
            }
            cout << "open file successfully.\n";

            // 獲得檔案大小
            fseek(fd, 0L, SEEK_END);
            int file_len = ftell(fd);
            fseek(fd, 0, SEEK_SET);

            if(!req.hasRange){  // 第一次的Request
                cout << "send headers with code 200.\n";
                snprintf(szRes, 4096, 
                    "HTTP/1.1 200 OK\nContent-Type: %s\n"
                    "Content-Length: %d\n"
                    "Accept-Ranges: bytes\n\n", 
                    type, file_len);
                send(conn, szRes, strlen(szRes), MSG_NOSIGNAL);
            }
            else{
                // 斷點續傳
                long content_left = file_len - req.start;
                int chunk_size = 1024*64;   // 64B
                int chunk_num = content_left / chunk_size;
                if(content_left % chunk_size != 0){
                    chunk_num++;
                }
                // 傳送檔案時全部都要傳，但是分成小部份多次
                for(int i = 0; i < chunk_num; i++){
                    if(i+1 == chunk_num){
                        chunk_size = file_len - req.start;
                    }
                    snprintf(szRes, 4096, 
                        "HTTP/1.1 206 Partial Content\n"
                        "Content-Type: %s\n"
                        "Content-Length: %d\n"
                        "Content-Range: bytes %d-%d/%d\n"
                        "Accept-Ranges: bytes\n\n", 
                        type, chunk_size, req.start, req.start+chunk_size-1, file_len);
                    send(conn, szRes, strlen(szRes), MSG_NOSIGNAL);

                    // 讀取檔案內容至容器內        
                    // 當宣告陣列時，需宣告大小，避免記憶體分配出現錯誤導致出錯
                    char szBuf[chunk_size]{'\0'};
                    // 跳到指定的範圍
                    fseek(fd, req.start, SEEK_SET);
                    if(fread(szBuf, 1, chunk_size, fd) == 0){
                        cout << "read file error.\n";
                        exit(1);
                    }
                    // Linux與Windows下的send返回值有差別，不需要檢查
                    send(conn, szBuf, sizeof(szBuf), MSG_NOSIGNAL);
                    req.start += chunk_size;
                    memset(szBuf, 0, sizeof(szBuf));
                }
            
                cout << "send file complete!\n\n";
            }
        }
        else{
            // 讀檔
            FILE *fd = fopen(req.filename, "r");
            if(fd == NULL){
                printf("%s file does not exist.\n", req.filename); 
                NotFound(conn);
                exit(1);     
            }
            cout << "open file successfully.\n";

            // 獲得文件大小
            fseek(fd, 0L, SEEK_END);
            int file_len = ftell(fd);
            fseek(fd, 0, SEEK_SET);

            // headers回傳
            snprintf(szRes, 4096, "HTTP/1.1 200 OK\nContent-Type: %s\nContent-Length: %d\nAccept-Ranges: bytes\n\n", type, file_len);
            send(conn, szRes, strlen(szRes), MSG_NOSIGNAL);
            cout << "send headers with code 200.\n";

            // 讀取檔案內容至容器內
            cout << "sending html...\n";
            // 當宣告陣列時，需宣告大小，避免記憶體分配出現錯誤導致出錯
            char szBuf[file_len]{'\0'};
            memset(szBuf, 0, sizeof(szBuf));

            if(fread(szBuf, 1, file_len, fd) == 0){
                cout << "read file error.\n";
                exit(1);
            }
            send(conn, szBuf, sizeof(szBuf), 0);
            cout << "sending successfully!\n";
        }       
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
        return -1;
    }
    
    // tell winsock the socket is for listening
    if(listen(sock, SOMAXCONN)<0){
        cerr << "listen error.\n";
        return -1;
    }

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
https://developerweb.net/viewtopic.php?id=5843 重要
https://www.twblogs.net/a/5c56d056bd9eee06ef3686e4 最重要的關鍵
https://www.kshuang.xyz/doku.php/programming:c:socket
https://bbs.csdn.net/topics/370091221          
https://www.itread01.com/content/1547180673.html
https://www.itread01.com/content/1548101702.html
https://www.itread01.com/p/168113.html
https://stackoverflow.com/questions/13043816/html5-video-and-partial-range-http-requests
https://stackoverflow.com/questions/9197689/invalid-conversion-from-int-to-socklen
https://iter01.com/68377.html 補充閱讀資料
*/