// 資料處理相關
#include <iostream>
#include <stdlib.h> // for strtol
#include <errno.h>  // for errno
#include <limits.h> // for INT_MAX, INT_MIN
#include <cstring>
#include <sstream>
#include <unistd.h>
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
    char  method[512];      // Request的方法
    char  filename[512];    // Request要求的檔案路徑
    char  version[512];     // HTTP的版本
    bool  hasRange;         // 用來判斷是否為Range Request
    int   start;            // 檔案的開始點
};

int PORT = 9527;

void BadRequest(int conn){  // 傳送 400 Bad Request
    char szRes[] = "HTTP/1.0 400 BAD REQUEST\nContent-Type: text/html\n\n";
    send(conn, szRes, strlen(szRes), MSG_NOSIGNAL);
    
    char body[] = "<!DOCTYPE html><html><head><title>400 BAD REQUEST</title></head><body><h1>400 BAD REQUEST</h1></body></html>";
    send(conn, body, strlen(body), 0);

    cout << "Bad request sended back.\n";
    exit(1);
}
void NotFound(int conn){    // 傳送 404 NOT FOUND
    char szRes[] = "HTTP/1.0 404 NOT FOUND\nContent-Type: text/html\n\n";
    send(conn, szRes, strlen(szRes), 0);
    
    char body[] = "<!DOCTYPE html><html><head><title>404 NOT FOUND</title></head><body><h1>404 NOT FOUND</h1></body></html>";
    send(conn, body, strlen(body), MSG_NOSIGNAL);

    cout << "404 NOT FOUND sended back.\n";
    exit(1);
}


void parseRequest(int conn, char* szReq, Target& req){  // Request內容
    // 檢查是否為Range Request
    char* ptr;
    if((ptr = strstr(szReq, "Range: bytes=")) == NULL){
        // 不是Range Request
        cout << "no range in request.\n";
        req.hasRange = false;   // 紀錄此Request不是Range Request
        req.start = 0;          // 開始點設為0，也就是文件開頭
    }
    else{
        // 是Range Request
        ptr = ptr + 13;         // 指針移動到第一個數字
        ptr = strtok(ptr, "-"); // 透過字串切割獲得檔案的開始點
        req.hasRange = true;    // 紀錄此Request為Range Request
        req.start = stoi(ptr);  // 字串轉換為文字並儲存開始點
    }

    // 獲取Request的方法
    char* split = strtok(szReq, " ");
    strncpy(req.method, split, 20);

    // 獲取Request要求的檔案路徑
    split = strtok(NULL, " ");
    if(split == NULL){  // Request缺少檔案路徑
        BadRequest(conn);
    }
    split++;  // 忽略字串開頭的斜線
    strncpy(req.filename, split, 200);

    // 獲取HTTP版本
    split = strtok(NULL, " ");
    if(split == NULL){  // Request缺少HTTP版本
        BadRequest(conn);
    }
    split = strtok(split, "\r\n");
    strncpy(req.version, split, 20);
}

void handleRequest(int conn){
    char szReq[4096]{'\0'}; 
    while(1){
        // 每一次接收資料前都必須清空Buffer
        memset(szReq, 0, sizeof(szReq));

        // 接收資料並存入Buffer
        int ret = read(conn, szReq, sizeof(szReq));
        if(ret == 0){       // 客戶端關閉
            printf("client closed.\n\n");
            break;
        }
        else if(ret == -1){ // 發生錯誤
            cerr << "read error.\n";
            exit(1);
        }
        fputs(szReq, stdout);

        // 開始解析Request
        // 宣告Class並擷取Request內容
        Target req;
        parseRequest(conn, szReq, req);

        // 判斷格式是否有錯誤，若有則回傳Bad Request
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
        if(strcmp(type, "video/mp4") == 0){ //如果要求的是一個影片
            // MP4檔案需以Binary模式開檔
            FILE *fd = fopen(req.filename, "rb+");
            if(fd == NULL){
                printf("%s file does not exist.\n", req.filename); 
                NotFound(conn);
            }
            cout << "open file successfully.\n";

            // 獲得檔案大小以供Header回傳Content-Length
            fseek(fd, 0L, SEEK_END);
            int file_len = ftell(fd);
            fseek(fd, 0, SEEK_SET);

            if(!req.hasRange){  // 第一次的Request不包含Range，必須回傳200
                snprintf(szRes, 4096, 
                    "HTTP/1.1 200 OK\n"
                    "Content-Type: %s\n"
                    "Content-Length: %d\n"
                    "Accept-Ranges: bytes\n\n", 
                    type, file_len);
                send(conn, szRes, strlen(szRes), MSG_NOSIGNAL);
            }
            else{  // 包含Range的Request，透過斷點續傳方式發送
                long content_left = file_len - req.start;   // 從Request要求的開始點到文件最後的大小
                int chunk_size = 1024*64;                   // 每個Response包含的檔案大小（64KB)
                int chunk_num = content_left / chunk_size;  // 總共需要多少的Response數量
                if(content_left % chunk_size != 0){         // 如果不整除，需要多一個Response處理餘數
                    chunk_num++;
                }
                // 傳送檔案時全部都要傳，但是分成小部份多次
                for(int i = 0; i < chunk_num; i++){
                    if(i+1 == chunk_num){
                        chunk_size = file_len - req.start;
                    }
                    //傳送Headers
                    snprintf(szRes, 4096, 
                        "HTTP/1.1 206 Partial Content\n"
                        "Content-Type: %s\n"
                        "Content-Length: %d\n"
                        "Content-Range: bytes %d-%d/%d\n"
                        "Accept-Ranges: bytes\n\n", 
                        type, chunk_size, req.start, req.start+chunk_size-1, file_len);
                    send(conn, szRes, strlen(szRes), MSG_NOSIGNAL);

                    // 宣告並讀取檔案內容至容器內
                    // 當宣告陣列時，需宣告大小，避免記憶體分配出現錯誤導致程式中斷 
                    char szBuf[chunk_size]{'\0'};
                    fseek(fd, req.start, SEEK_SET);         // 跳到指定的開始點
                    if(fread(szBuf, 1, chunk_size, fd) == 0){
                        cout << "read file error.\n";
                        exit(1);
                    }
                    // 傳送檔案
                    // Linux與Windows下的send返回值有差別，不需要檢查
                    // 使用flag: MSG_NOSIGNAL可以避免不必要的錯誤
                    send(conn, szBuf, sizeof(szBuf), MSG_NOSIGNAL);
                    req.start += chunk_size;                // 傳送完成後，讓開始點的位置移動
                    memset(szBuf, 0, sizeof(szBuf));        // 清空Buffer
                }
            
                cout << "send file complete!\n\n";
            }
        }
        else if(strcmp(type, "text/html") == 0){   // 如果要求的是一個文件
            // html文件開檔
            FILE *fd = fopen(req.filename, "r");
            if(fd == NULL){
                printf("%s file does not exist.\n", req.filename); 
                NotFound(conn);
                exit(1);     
            }

            // 獲得文件大小以供Header回傳Content-Length
            fseek(fd, 0L, SEEK_END);
            int file_len = ftell(fd);
            fseek(fd, 0, SEEK_SET);

            // headers回傳
            snprintf(szRes, 4096, "HTTP/1.1 200 OK\nContent-Type: %s\nContent-Length: %d\nAccept-Ranges: bytes\n\n", type, file_len);
            send(conn, szRes, strlen(szRes), MSG_NOSIGNAL);

            // 讀取檔案內容至容器內
            // 當宣告陣列時，需宣告大小，避免記憶體分配出現錯誤導致出錯
            char szBuf[file_len]{'\0'};
            memset(szBuf, 0, sizeof(szBuf));
            if(fread(szBuf, 1, file_len, fd) == 0){
                cout << "read file error.\n";
                exit(1);
            }
            // 傳送檔案內容
            send(conn, szBuf, sizeof(szBuf), 0);
            cout << "send file complete!\n\n";
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
        // 檢查Port範圍是否正確
        if (arg < 0 || arg > 65535){
            cerr << "Port number out of range.\n";
            return -1;
        }
        else if (arg >= 0 && arg <= 1023 && arg != 80){
            cerr << "Privileged services port number.\n";
            return -1;
        }
        else{
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
    
    // the socket is for listening
    if(listen(sock, SOMAXCONN)<0){
        cerr << "listen error.\n";
        return -1;
    }

    // new sockaddr for client
    sockaddr_in client;
    socklen_t clientsize = sizeof(client);
    int conn;

    pid_t id;

    while(1){
        // 等待連線
        conn = accept(sock, (sockaddr *)&client, &clientsize);  // 三次握手
        if (conn == -1)
            return -1;
        printf("recv connect ip=%s port=%d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        
        // 多線程使用fork()，分裂出子線程處理資料，父線程繼續監聽
        id = fork();

        if(id == -1){
            cerr << "fork error.\n";
            return -1;
        }
        if(id == 0){   // 子程序
            // 關閉監聽連線
            close(sock);
            handleRequest(conn);
            exit(0);
        }
        else if(id > 0){    // 父程序
            // 不需要用到新的conn，關閉連線
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
https://www.cnblogs.com/dyllove98/p/3151162.html 補充閱讀資料
*/