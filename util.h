#ifndef UTIL
#define UTIL
#include <cstdlib>

ssize_t readn(int fd,void* buff,size_t n);  //规定读取n个字节到buff
ssize_t writen(int fd,void* buff,size_t n);  //规定写n个字节到buff
void handle_for_sigpipe();
int setSocketNonBlocking(int fd);

#endif