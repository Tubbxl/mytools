
#include <iostream>
#include <stdint.h> // for uint8_t
#include <string.h> // for memset()
#include <errno.h>
#include <fcntl.h>	// for O_RDWR
#include <stdio.h> // for fprintf()
#include <stdlib.h>	// for exit()
#include <termios.h>
#include <unistd.h>


#include "ai_service/device_io.h"
#include "ai_service/sender_xmodem.h"
#include "muyuan_robot_utils/log4.h"
using namespace std;

void error_printer (const char* functionCall, const char* file, int line, int error)
{
	log_error("!!! Error %d (%s) occurred at line %d of file %s\n \t resulted from invocation: %s",
				error, strerror(error), line, file, functionCall);
}

unsigned short updcrc(register int c, register unsigned crc)
{
	register int count;

	for (count=8; --count>=0;) {
		if (crc & 0x8000) {
			crc <<= 1;
			crc += (((c<<=1) & 0400)  !=  0);
			crc ^= 0x1021;
		}
		else {
			crc <<= 1;
			crc += (((c<<=1) & 0400)  !=  0);
		}
	}
	return crc;
}

void PeerX::crc16ns (uint16_t* crc16nsP, uint8_t* buf)
{
	 register int wcj;
	 register uint8_t *cp;
	 unsigned oldcrc=0;
	 for (wcj=CHUNK_SZ,cp=buf; --wcj>=0; ) {
		 oldcrc=updcrc((0377& *cp++), oldcrc);

	 }
		 oldcrc=updcrc(0,updcrc(0,oldcrc));
		 *crc16nsP = (oldcrc >> 8) | (oldcrc << 8);
}

PeerX::PeerX(int d)
:result("ResultNotSet"), mediumD(d), transferringFileD(-1), crc_flg(true)
{
}

char PeerX::get_byte()
{
    char byte;
    int retVal = read(mediumD, &byte, 1);
    if(retVal == 1){
        return byte;
    }
    return 0x00;
}


void PeerX::send_byte(uint8_t byte)
{
	switch (int retVal = write(mediumD, &byte, sizeof(byte))) {
		case 1:
			return;
		case -1:
			error_printer("device_write(mediumD, &byte, sizeof(byte))", __FILE__, __LINE__, errno);
			break;
		default:
			std::cout << "Wrong number of bytes written: " << retVal << std::endl;
			exit(EXIT_FAILURE);
	}
}

bool PeerX::flush(){
   return 0 == tcflush(mediumD,TCIOFLUSH);
}

SenderX::SenderX(int d)
:PeerX(d), bytesRd(-1), blkNum(255)
{
}

void SenderX::genBlk(blkT blkBuf)
{
	if (-1 == (bytesRd = read(transferringFileD, &blkBuf[3], CHUNK_SZ )))
		error_printer("device_read(transferringFileD, &blkBuf[3], CHUNK_SZ )", __FILE__, __LINE__, errno);

	if (bytesRd != -1 && bytesRd != CHUNK_SZ)
    {
    	for(int ii=bytesRd; ii < CHUNK_SZ; ii++)
    	{
    		blkBuf[3+ii] = CTRL_Z; // As per xmodem-edited.txt
    	}

    }
	blkBuf[0] = SOH;
    blkBuf[1] = blkNum;
    blkBuf[2] = 0xFF - blkNum;

    uint8_t checksum = 0x00;

    if(crc_flg)
    {
    	crc16ns((uint16_t*)&blkBuf[3+CHUNK_SZ], &blkBuf[3]);

    }
    else
    {
    	for(int ii = 0; ii < CHUNK_SZ; ii++)
    	{
    		checksum += blkBuf[3+ii];
    	}

    	blkBuf[3+CHUNK_SZ] = checksum;
    }

}

void SenderX::send_file(const char* file)
{
	transferringFileD = open(file, O_RDWR, 0);
	if(transferringFileD == -1) {
		log_error("Error opening input file named: [%s]",file);
		result = "OpenError";
	}
	else {
		log_info("Sender will send [%s]",file);

		blkNum = 1; 

		genBlk(blkBuf);
		int retry = 100;
		while (bytesRd && retry >0)
		{
			if(crc_flg)
			{
				for (int ii = 0; ii < BLK_SZ_CRC; ii++)
				{
					send_byte(blkBuf[ii]);
				}
			}
			else
			{
				for (int ii = 0; ii < BLK_SZ; ii++)
				{
					send_byte(blkBuf[ii]);
				}
			}
                char tm = get_byte();
                if( tm == ACK){
                    log_debug("Ack [%d]",blkNum);
                    blkNum ++;
                    genBlk(blkBuf);
                    retry = 100;
                }else{
                    retry --;
                }
		}
		send_byte(EOT);
		send_byte(EOT);
		if (-1 == close(transferringFileD))
			error_printer("myClose(transferringFileD)", __FILE__, __LINE__, errno);
		if(retry<1){
			result = "TimeOut";
			return;
		}
		result = "Done";
	}
}
