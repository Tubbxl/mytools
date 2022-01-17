#ifndef __TFTP__H__
#define __TFTP__H__

#define RRQ 0x01
#define WRQ 0x02
#define DATA 0x03
#define ACK 0x04
#define ERR 0x05
//#define ERROR -1
#define MAXREQPACKET 256
#define MAX_FILE_BUFFER 1024
#define MAX_RETRY 5

int send_file_to_mcu(const char* file, const char* ip, int port);

#endif
