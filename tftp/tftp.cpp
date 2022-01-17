#include "tftp.h"  //some constants have been defined there
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdlib>
#include <iostream>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <vector>

#include "update_progress.h"

using namespace std;

char ERROR_MESSAGE[8][256] = {"Not defined, see error message (if any).\0",
                              "File not found.\0",
                              "Access violation.\0",
                              "Disk full or allocation exceeded.\0",
                              "Illegal TFTP operation.\0",
                              "Unknown transfer ID.\0",
                              "File already exists.\0",
                              "No such user.\0"};
struct timeval time_channel, start_time, end_time;

bool validation(int argc);
int connect_to_server(const char *ip, int port, struct sockaddr_in &server_addr);
void display_server_details(struct sockaddr_in addr);
bool req_packet(unsigned char opcode, char *filename, unsigned char request_buf[],
                int *request_length);
void get_file(char filename[], struct sockaddr_in server_addr, int socket);
void post_file(char filename[], struct sockaddr_in server_addr, int socket);
int ack_packet(int block, unsigned char ack_buf[]);
int error_packet(unsigned char error_buf[], unsigned char error_code);
void clean(unsigned char buffer[]);

char current_dir[256];
bool debug = true;
int window_size = 1;
/*
    ** server_address port <-g filename> <-p filename>
*/
int send_file_to_mcu(const char *file, const char *ip, int port)
{
    time_channel.tv_sec = 10;
    time_channel.tv_usec = 0;
    int client_socket = -1;
    struct hostent *server = NULL;
    struct sockaddr_in server_addr;

    server = gethostbyname(ip);
    if (server == NULL) {
        perror(" Can not get server by hostname. Please try to use IP format");
        return -1;
    }
    if (debug) {
        cout << " Server has been found successfully:" << server->h_name << endl;
    }
    bzero((char *)&server_addr, sizeof(server_addr));  // clean the var
    if ((client_socket = connect_to_server(ip, port, server_addr)) < 0) {
        perror(" Can not connect to the server or socket error");
        return -1;
    }
    display_server_details(server_addr);
    unsigned char opcode = 0x06;
    char filename[256] = {0};
    getcwd(current_dir, 256);
    strcat(current_dir, "/");
    opcode = WRQ;
    strcpy(filename, file);

    char request_buf[MAXREQPACKET];
    int request_length;
    /*
        ** request packet will be sent to server request can be RRQ or WRQ
    */
    log_info("---------------->file:%s",filename);
    if (!req_packet(opcode, filename, (unsigned char *)&request_buf, &request_length)) {
        perror(" Can not create request packet ");
        close(client_socket);
        return -1;
    }
    if (debug)
        cout << " Request Packet has been created " << endl;
    int request_send_status = -1;
    if (debug)
        log_info("Opcode : %02x", request_buf[1]);
    if ((request_send_status = sendto(client_socket, request_buf, request_length, 0,
                                      (const struct sockaddr *)&server_addr, sizeof(server_addr))) <
        0) {
        perror(" Can not send request packet ");
        close(client_socket);
        return -1;
    }
    if (debug)
        cout << " Successfully sent request packet ==> " << request_send_status << endl;
    switch (opcode) {
        case RRQ:
            gettimeofday(&start_time, NULL);
            get_file(filename, server_addr, client_socket);
            gettimeofday(&end_time, NULL);
            break;
        case WRQ:
            gettimeofday(&start_time, NULL);
            post_file(filename, server_addr, client_socket);
            gettimeofday(&end_time, NULL);
            break;
        default:
            cout << " No predefined function " << endl;
            close(client_socket);
            return -1;
    }
    close(client_socket);
    log_info("Total operation took => %d millisecond(s)\n",
           (int)((end_time.tv_sec - start_time.tv_sec) * 1000 +
                 (end_time.tv_usec - start_time.tv_usec) / 1000));
    return 0;
}
int connect_to_server(const char *ip, int port, struct sockaddr_in &server_addr)
{
    bzero((char *)&server_addr, sizeof(server_addr)); /*Clear the structure */
    server_addr.sin_family = AF_INET;                 /*address family for TCP and UDP */
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
    int client_socket = -1;
    client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return client_socket;
}

void display_server_details(struct sockaddr_in addr)
{
    if (debug) {
        log_info("%s\n", "=================================================================");
        log_info("%s\n", "You have successfully connected with the server . Details are as below : ");
        log_info(" Server Ip -> %s\n", inet_ntoa(addr.sin_addr));
        log_info(" Server Port -> %d\n", ntohs(addr.sin_port));
    }
}
bool req_packet(unsigned char opcode, char *filename, unsigned char request_buf[],
                int *request_length)
{
    std::string file(filename);
    vector<string> vStr;
    boost::split(vStr, file, boost::is_any_of("/"), boost::token_compress_on);
    cout << "file name:" << vStr[vStr.size() - 1] << endl;

    int packet_length = sprintf((char *)request_buf, "%c%c%s%c%s%c", 0x00, opcode,
                                vStr[vStr.size() - 1].c_str(), 0x00, "octet", 0x00);
    // int packet_length = sprintf((char
    // *)request_buf,"%c%c%s%c%s%c",0x00,opcode,filename,0x00,"octet",0x00);
    if (packet_length > 0) {
        if (debug)
            log_info("Request packet length => %d  file name:%s\n", packet_length, filename);
        *request_length = packet_length;
    }
    return (packet_length > 0);
}
void get_file(char filename[], struct sockaddr_in server_addr, int socket)
{
    if (debug) {
        display_server_details(server_addr);
        fprintf(stdout, "================================\n%s\n=====================\n",
                "You are going to Download File");
        fprintf(stdout, "The File => %s\n", filename);
    }
    int no_of_retry, server_response, request_length, received_packet, TID, error_length,
        data_section, ack_length, intend_packet;
    unsigned char buffer[MAX_FILE_BUFFER], file_buffer[MAX_FILE_BUFFER];
    unsigned char request_buf[256], operation_buf[256];
    clean(buffer);
    clean(request_buf);
    clean(operation_buf);
    clean(file_buffer);
    int server_length = sizeof(server_addr);
    req_packet(RRQ, filename, request_buf, &request_length);
    received_packet = 0;
    intend_packet = 1;
    TID = 0;
    char *response_handler = NULL;
    unsigned char server_opcode;
    data_section = 512;
    server_response = data_section + 4;
    FILE *fp = NULL;
    if ((fp = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "%s\n", "Can not open file in  client side");
        return;
    }
    while (server_response == data_section + 4) {
        for (no_of_retry = 0; no_of_retry < MAX_RETRY; no_of_retry++) {
            if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const struct timeval *)&time_channel,
                           sizeof(struct timeval)) < 0) {
                perror("setsockopt() error");
            }
            server_response =
                recvfrom(socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr,
                         (socklen_t *)&server_length);
            if (server_response < 0) {
                if (received_packet == 0) {
                    if (sendto(socket, request_buf, request_length, 0,
                               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                        fprintf(stderr, "%s\n", "can not send RRQ to server");
                    }
                } else {
                    intend_packet = (intend_packet == 1) ? intend_packet : intend_packet - 1;
                    ack_length = ack_packet(intend_packet, operation_buf);
                    intend_packet = (intend_packet == 1) ? intend_packet : intend_packet + 1;
                    if (sendto(socket, operation_buf, ack_length, 0,
                               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                        fprintf(stderr, "%s\n", "Can not send re ack for data packet");
                    }
                }
                continue;
            }
            /*
                **  store the TID of the coming server response if response > 0
            */
            if (!TID) {
                TID = ntohs(server_addr.sin_port);
                if (debug) {
                    log_info("The server TID => %d\n", TID);
                }
            }
            /*
                *** different transmission identifier
            */
            if (server_addr.sin_port != htons(TID)) {
                fprintf(stderr, "%s\n", "Different TID");
                error_length = error_packet(operation_buf, 0x05);
                if (sendto(socket, operation_buf, error_length, 0, (struct sockaddr *)&server_addr,
                           sizeof(server_addr)) < 0) {
                    fprintf(stderr, "%s\n", "can not send Error packet to server");
                }
                no_of_retry--;
                continue;
            }
            /*
                ** fetch the server response and write into file
            */
            else {
                response_handler = (char *)buffer;
                response_handler++;
                server_opcode = *response_handler;
                response_handler++;
                received_packet = *response_handler << 8;
                received_packet &= 0xff00;
                response_handler++;
                received_packet += *response_handler & 0x00ff;
                if (debug) {
                    log_info("The opcode => %02x packet sent by server => %d\n", server_opcode,
                           received_packet);
                }
                response_handler++;
                memcpy((char *)file_buffer, response_handler, server_response - 4);
                if (server_opcode != DATA) {
                    fprintf(stderr, " Server Error => %s\n", file_buffer);
                    if (server_opcode > ERR) {
                        error_length = error_packet(operation_buf, 0x04);
                        if (sendto(socket, operation_buf, error_length, 0,
                                   (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                            fprintf(stderr, "%s\n",
                                    "Can not send errorPacket of illegal TFTP operation");
                        }
                    }
                    continue;
                }
                if (received_packet != intend_packet) {
                    log_info("Actual packet no => %d / Received packet no => %d\n", intend_packet,
                           received_packet);
                    ack_length = ack_packet(intend_packet, operation_buf);
                    if (sendto(socket, operation_buf, ack_length, 0,
                               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                        fprintf(stderr, "%s\n", "Can not send re ack for data packet");
                    }
                    no_of_retry--;
                    continue;
                }
                /*
                    actual block received
                */
                if (fwrite(file_buffer, 1, data_section, fp) != (unsigned int)data_section) {
                    fprintf(stderr, "%s\n", "Can not perform fwrite accurately");
                    return;
                }
                /*
                    ** create next ack packet and send that to server
                */
                ack_length = ack_packet(intend_packet, operation_buf);
                intend_packet++;
                if (sendto(socket, operation_buf, ack_length, 0, (struct sockaddr *)&server_addr,
                           sizeof(server_addr)) < 0) {
                    fprintf(stderr, "%s\n", "Can not sent acknowledgement to Server");
                    intend_packet--;
                } else {
                    if (debug)
                        log_info("Succesfully sent ack for next packet no => %d\n", intend_packet);
                    break;
                }
            }
        }
        if (no_of_retry == MAX_RETRY) {
            log_info("Max retry attempt exhausted ... :(\n");
            return;
        }
    }
    if (server_response < data_section + 4) {
        if (fp)
            fclose(fp);
        sync();
        log_info("Successfully downloaded file from server ... :)\n");
    }
}

void post_file(char filename[], struct sockaddr_in server_addr, int socket)
{
    if (debug) {
        display_server_details(server_addr);
        fprintf(stdout, "================================\n%s\n=========================\n",
                "You are going to Upload File");
        fprintf(stdout, "Post The File => %s\n", filename);
    }
    int no_of_retry, server_response, sent_packet, request_length, server_length, TID, error_length,
        file_read, acked_packet, data_length;
    unsigned char ack_buffer[MAX_FILE_BUFFER], request_buf[256], operation_buf[256], server_opcode,
        file_buffer[MAX_FILE_BUFFER], data_buffer[window_size][516];
    bool last_ack = false;
    sent_packet = acked_packet = 0;
    req_packet(WRQ, filename, request_buf, &request_length);
    server_length = sizeof(server_addr);
    TID = 0;
    char *response_handler = NULL;
    const int data_section = 512;
    FILE *fp = NULL;
    if ((fp = fopen(filename, "r")) == NULL) {
        log_info("open file failed\n");
        fprintf(stderr, "%s\n", "Can not open file in client side");
        return;
    }
    fseek(fp,0L,SEEK_END);
    long filesize = ftell(fp);
    int packet_size = (filesize%512) ? (filesize/512)+1 : (filesize/512);
    log_info("pack len = %d \n",packet_size);
    fseek(fp,0L,SEEK_SET);
    UpdateProgress* send_progress = UpdateProgress::get_instance();
    for (no_of_retry = 0; no_of_retry < MAX_RETRY; no_of_retry++) {
        if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const struct timeval *)&time_channel,
                       sizeof(struct timeval)) < 0) {
            perror("setsockopt() error");
        }
        server_response = recvfrom(socket, ack_buffer, sizeof(ack_buffer), 0,
                                   (struct sockaddr *)&server_addr, (socklen_t *)&server_length);
        if (server_response < 0) {
            log_info("server_response < 0 \n");
            if (acked_packet == 0) {
                // if(sendto(socket,request_buf,request_length,0,(struct sockaddr
                // *)&server_addr,sizeof(server_addr)) < 0){
                // 	fprintf(stderr, "%s\n","Can not send write request to server" );
                // }
            }
            continue;
        }
        /*
            ** got response > 0
        */
        fprintf(stderr, "info:%s\n", ack_buffer + 3);
        if (!TID) {
            TID = ntohs(server_addr.sin_port);
        }
        if (server_addr.sin_port != htons(TID)) {
            /*
                ** Unknown TID error
            */
            error_length = error_packet(operation_buf, 0x05);
            if (sendto(socket, operation_buf, error_length, 0, (struct sockaddr *)&server_addr,
                       sizeof(server_addr)) < 0) {
                fprintf(stderr, "%s\n", "Can not send Unknown TID error");
            }
            no_of_retry--;
            continue;
        }
        /*
            ** Got valid response
        */
        response_handler = (char *)ack_buffer;
        response_handler++;
        server_opcode = *response_handler;
        response_handler++;
        acked_packet = *response_handler << 8;
        acked_packet &= 0xff00;
        response_handler++;
        acked_packet += *response_handler & 0x00ff;
        response_handler++;
        if (debug)
            log_info("Opcode => %02x block => %d\n", server_opcode, sent_packet);
        if (server_opcode != ACK) {
            fprintf(stderr, "Not a data packet =>%s\n", response_handler + 4);
            if (server_opcode > ERR) {
                error_length = error_packet(operation_buf, 0x04);
                if (sendto(socket, operation_buf, error_length, 0, (struct sockaddr *)&server_addr,
                           sizeof(server_addr)) < 0) {
                    fprintf(stderr, "%s\n", "Can not sent Illegal TFTP operation error message");
                }
            }
            continue;
        }
        /*
            ** Got valid ACK with sent_packet = 0;
        */
        break;
    }

    if (no_of_retry == MAX_RETRY) {
        fprintf(stderr, "%s\n", "Max retry attempt exhausted ....... :(");
        return;
    }
    file_read = data_section;

    clean(file_buffer);

    for (int i = 0; i < window_size; ++i) {
        // clean(data_buffer[i]);
        memset(data_buffer[i], 0x00, 516);
    }

    data_length = data_section + 4;
    while (!last_ack) {
        if (acked_packet == sent_packet) {
            for (int i = 0; i < window_size && !feof(fp); ++i) {
                sent_packet++;
                file_read = fread(file_buffer, 1, data_section, fp);
                data_length = sprintf((char *)data_buffer[i], "%c%c%c%c", 0x00, DATA, 0x00, 0x00);
                memcpy((char *)data_buffer[i] + 4, file_buffer, data_section);
                data_buffer[i][2] = (sent_packet & 0xff00) >> 8;
                data_buffer[i][3] = sent_packet & 0x00ff;

                // if(sendto(socket,data_buffer[i],data_section+4,0,(struct sockaddr
                // *)&server_addr,sizeof(server_addr)) < 0){
                if (sendto(socket, data_buffer[i], file_read + 4, 0,
                           (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                    fprintf(stderr, "%s\n", "Can not send data packet1");
                }

                send_progress->send_update_progress(((float)sent_packet/(float)packet_size)*100);
                if (debug)
                    log_info(
                        "Data block => %d has sent.....Waiting for acknowledgement => %d == %d\n",
                        sent_packet, data_length, file_read);
            }
        }

        for (no_of_retry = 0; no_of_retry < MAX_RETRY; no_of_retry++) {
            if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const struct timeval *)&time_channel,
                           sizeof(struct timeval)) < 0) {
                perror("setsockopt() error");
            }
            server_response =
                recvfrom(socket, ack_buffer, sizeof(ack_buffer), 0, (struct sockaddr *)&server_addr,
                         (socklen_t *)&server_length);
            if (server_response < 0) {
                for (int i = acked_packet; i <= sent_packet; ++i) {
                    if (sendto(socket, data_buffer[i], data_section + 4, 0,
                               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                        fprintf(stderr, "%s\n", "Can not send data packet 2 ");
                    }
                }
                continue;
            }

            if (server_addr.sin_port != htons(TID)) {
                fprintf(stderr, "%s\n", "Unknown Identifier");
                error_length = error_packet(operation_buf, 0x05);
                if (sendto(socket, operation_buf, error_length, 0, (struct sockaddr *)&server_addr,
                           sizeof(server_addr)) < 0) {
                    fprintf(stderr, "%s\n", "Can not send error packet to server");
                }
                continue;
            }
            /*
                ** Got valid response
            */
            // fprintf(stderr, "info1:%s\n", ack_buffer + 4);
            response_handler = (char *)ack_buffer;
            response_handler++;
            server_opcode = *response_handler;
            response_handler++;
            acked_packet = *response_handler << 8;
            acked_packet &= 0xff00;
            response_handler++;
            acked_packet += *response_handler & 0x00ff;
            response_handler++;
            if (server_opcode != ACK) {
                fprintf(stderr, "Not a ack packet => %s\n", response_handler + 4);
                if (server_opcode > ERR) {
                    error_length = error_packet(operation_buf, 0x04);
                    if (sendto(socket, operation_buf, error_length, 0,
                               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                        fprintf(stderr, "%s\n", "Can not send error packet to server");
                    }
                }
                fprintf(stderr, "%s  %d\n", "Aborting ........... :( ", server_opcode);
                continue;  // return;
            }
            /*
                ** correct ack packet
            */
            if (feof(fp)) {
                if (acked_packet == sent_packet) {
                    log_info("%s\n", "Successfully Uploaded data ..... :)");
                    return;
                }
            }
            if (debug) {
                log_info("Data block => %d has been sent and acknowledged\n", acked_packet);
            }
            break;
        }
    }
}
int ack_packet(int block, unsigned char ack_buf[])
{
    /*
        ** 4 Byte data:
            *** 2 byte opcode 0x0004
            *** 2 byte block number
    */
    int packet_length = sprintf((char *)ack_buf, "%c%c%c%c", 0x00, ACK, 0x00, 0x00);
    ack_buf[2] = (block & 0xff00) >> 8;
    ack_buf[3] = (block & 0x00ff);
    if (debug)
        log_info(
            " After Receiving ==> Ack packet Length : %d == Opcode : %02x ---- Block number : "
            "%02x:%02x\n",
            packet_length, ACK, ack_buf[2], ack_buf[3] & 0x00ff);
    return packet_length;
}
int error_packet(unsigned char error_buf[], unsigned char error_code)
{
    int length = sprintf((char *)error_buf, "%c%c%c%c%s%c", 0x00, ERR, 0x00, error_code,
                         ERROR_MESSAGE[error_code], 0x00);
    return length;
}
void clean(unsigned char buffer[])
{
    size_t length = strlen((char *)buffer);
    bzero((char *)&buffer, length);
}
