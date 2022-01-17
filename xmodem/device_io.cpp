#include "muyuan_robot_utils/log4.h"
#include <unistd.h>			// for read/write/close
#include <fcntl.h>	// for open/creat
#include <arpa/inet.h>
#include <time.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

int device_open(const char *dev_name)
{
	struct termios opt;
    int dev_handle = open(dev_name,O_RDWR);
    if(dev_handle < 0)
    {
        dev_handle = -1;
        log_info("%s can not open",dev_name);
        return -1;
    }
    log_info("COM opened successed %d",dev_handle);
    
	tcgetattr(dev_handle, &opt);      
    cfsetispeed(&opt, B115200);
    cfsetospeed(&opt, B115200);

    opt.c_lflag   &=   ~(ECHO   |   ICANON   |   IEXTEN   |   ISIG);
    opt.c_iflag   &=   ~(BRKINT   |   ICRNL   |   INPCK   |   ISTRIP   |   IXON);
    opt.c_oflag   &=   ~(OPOST);
    opt.c_cflag   &=   ~(CSIZE   |   PARENB);
    opt.c_cflag   |=   CS8;

    opt.c_cc[VMIN]   =   0;                                      
    opt.c_cc[VTIME]  =   5;

    if (tcsetattr(dev_handle,   TCSANOW,   &opt)<0) {
        return   -1;
    }
    return dev_handle;
}

ssize_t device_read( int fildes, void* buf, size_t nbyte )
{
	return read(fildes, buf, nbyte );
}

ssize_t device_write( int fildes, const void* buf, size_t nbyte )
{
	return write(fildes, buf, nbyte );
}

int device_close( int fd )
{
	return close(fd);
}
