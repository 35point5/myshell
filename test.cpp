//
// Created by Mogician on 2022/8/6.
//

#include <unistd.h>
#include <iostream>
#include <cstdio>
#include <pwd.h>

using namespace std;
const size_t kBufferSize=4096;
char buf[kBufferSize];
int main() {
    readlink("/proc/self/exe", buf, kBufferSize);
    cout << buf << endl;
}