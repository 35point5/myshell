#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#define STR_WRITE(fd, str) write(fd,str.data(),str.length()) //输出string到指定fd
#define CHARS_WRITE(fd, chars) write(fd,chars,strlen(chars)) //输出char*到指定fd
#include <fstream>
#include "string.h"
using namespace std;
#define DUP(old_fd, new_fd) dup2(old_fd, new_fd); close(old_fd);
int main(){
    int fds[2];
    pipe(fds); //创建管道
    if (!fork()) {
        DUP(fds[0], STDIN_FILENO); //将管道的读端复制到标准输入
        close(fds[1]); //关闭管道的写端
        execlp("/usr/bin/more",   nullptr); //执行more命令
        exit(0);
    } else {
        CHARS_WRITE(fds[1],"6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n6666666666666666666666\n");
        close(fds[0]);
        close(fds[1]);
        wait(nullptr); //等待子进程结束
        cout<<"@#@#@#@#@3"<<endl;
    }
}