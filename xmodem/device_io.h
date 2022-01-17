#pragma once
#include <unistd.h>
#include <sys/stat.h>
int device_open(const char *dev_name);
ssize_t device_read( int fildes, void* buf, size_t nbyte );
ssize_t device_write( int fildes, const void* buf, size_t nbyte );
int device_close( int fd );

