#include "server.h"
#include "types.h"
#include "socket.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define LISTENING_SOCKET_COUNT  4

typedef struct socket_data_t
{
    uint8_t socket_id;
    uint16_t listening_port;
    bool socket_open;
    uint8_t receive_buffer[BUFFER_SIZE];
    uint16_t receive_size;
    uint8_t send_buffer[BUFFER_SIZE];
    uint16_t send_size;
} socket_data_t;

void server_loop(socket_data_t* socket_info);

void server_task(void* params)
{
    server_data_t* server_data = (server_data_t*) params;
    socket_data_t socket_data[LISTENING_SOCKET_COUNT];

    printf("Tcp server waiting for ip...\n");
    xSemaphoreTake(server_data->ip_assigned_sem, portMAX_DELAY);
    printf("IP Assigned, starting tcp server.\n");

    //Initialise socket data
    for(int i = 0; i < LISTENING_SOCKET_COUNT; ++i)
    {
        socket_data[i].socket_id = BASE_PORT_ID + i;
        socket_data[i].listening_port = LISTENING_PORT;
        socket_data[i].socket_open = false;

        socket(socket_data[i].socket_id, Sn_MR_TCP, socket_data[i].listening_port, 0x0);
        server_loop(&socket_data[i]);
    }

    while(1)
    {
        for(int i = 0; i < LISTENING_SOCKET_COUNT; ++i)
        {
            server_loop(&socket_data[i]);
        }
    }
}

void server_loop(socket_data_t* socket_info)
{
    long ret = 0;
    uint16_t size = 0;

    //while (1)
    {

        switch(getSn_SR(socket_info->socket_id))
        {
            case SOCK_ESTABLISHED :
                {
                    if (!socket_info->socket_open)
                    {
                        socket_info->socket_open = true;
                        char* welcomeString = "Welcome....\r\n";
                        ret = send(socket_info->socket_id, (uint8_t *)welcomeString, strlen((const char *)welcomeString));

                        if(ret < 0)
                        {
                            socket_info->socket_open = false;
                            close(socket_info->socket_id);
                            return;//ret;
                        }

                        printf("[%d]: SOCK_ESTABLISHED\n",socket_info->socket_id);
                    }
                }

                if((size = getSn_RX_RSR(socket_info->socket_id)) > 0) // Don't need to check SOCKERR_BUSY because it doesn't not occur.
                {

                    memset(socket_info->receive_buffer, 0, BUFFER_SIZE);

                    ret = recv(socket_info->socket_id, socket_info->receive_buffer, size);
                    socket_info->receive_buffer[ret] = '\0';
                    if(ret != size)
                    {
                        if(ret == SOCK_BUSY) return;// 0;
                        if(ret < 0)
                        {
                            socket_info->socket_open = false;
                            close(socket_info->socket_id);
                            return;// ret;
                        }
                    }
                    else
                    {
                        printf("%s", socket_info->receive_buffer);
                    }
                }
                break;

            case SOCK_CLOSE_WAIT :
                printf("[%d]: SOCK_CLOSE_WAIT\n",socket_info->socket_id);
                ret=disconnect(socket_info->socket_id);

                if(ret != SOCK_OK)
                {
                    return;
                }

                socket_info->socket_open = false;
                break;

            case SOCK_CLOSED :
                printf("[%d]: SOCK_CLOSED\n",socket_info->socket_id);
                if((ret=socket(socket_info->socket_id, Sn_MR_TCP, socket_info->listening_port, 0x0)) != socket_info->socket_id)
                {
                    socket_info->socket_open = false;
                    close(socket_info->socket_id);
                    return;
                }
                break;

            case SOCK_INIT :
                printf("[%d]: SOCK_INIT\n",socket_info->socket_id);
                if( (ret = listen(socket_info->socket_id)) != SOCK_OK)
                {
                    return;
                }

                break;

            default :
                break;
        }
    }
}