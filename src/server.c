#include "server.h"
#include "types.h"
#include "socket.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef struct socket_data_t
{
    uint8_t socket_id;
    uint16_t listening_port;
    bool socket_open;
    uint8_t receive_buffer[BUFFER_SIZE];
    uint16_t receive_size;
    uint8_t send_buffer[BUFFER_SIZE];
    uint16_t send_size;
    uint64_t last_command_received;
    uint64_t last_command_send;
} socket_data_t;

void server_loop(socket_data_t* socket_info);
bool handle_receive_bufffer(socket_data_t* socket_info, message_t* message);

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
        socket_data[i].receive_size = 0;
        socket_data[i].send_size = 0;

        socket(socket_data[i].socket_id, Sn_MR_TCP, socket_data[i].listening_port, 0x0);
        server_loop(&socket_data[i]);
    }

    while(1)
    {
        message_t send_message;
        send_message.message_type = NO_MESSAGE;

        xQueueReceive(server_data->send_queue, (void *)&send_message,  ( TickType_t ) 0);

        for(int i = 0; i < LISTENING_SOCKET_COUNT; ++i)
        {
            //If the socket is open, check if we need to send a command or heartbeat
            if (socket_data[i].socket_open)
            {
                //If the message is for this socket
                if (send_message.client == socket_data[i].socket_id)
                {
                    if (send_message.message_type != NO_MESSAGE)
                    {
                        if (send_message.message_type == MSG_CURRENT_SPEEED)
                        {
                            socket_data[i].send_size = sprintf((char*)socket_data[i].send_buffer, "S%d#", send_message.value);
                        }
                        if (send_message.message_type == MSG_REMAINING_TIME)
                        {
                            socket_data[i].send_size = sprintf((char*)socket_data[i].send_buffer, "T%d#", send_message.value);
                        }
                    }
                }

                //No need to send data, check if we need to send a heartbeat
                if (socket_data[i].send_size == 0)
                {
                    uint64_t now = time_us_64();
                    uint64_t last_command_send_time = (now - socket_data[i].last_command_send);

                    if (last_command_send_time  > (KEEP_ALIVE_SECONDS * 1000 * 1000))
                    {
                        printf("[%d]: Sending heartbeat.\n",i);
                        socket_data[i].send_size = sprintf((char*)socket_data[i].send_buffer, "HB#", send_message.value);
                    }
                }
            }

            server_loop(&socket_data[i]);

            message_t received_message;
            //Check for received TCP messages
            while(handle_receive_bufffer(&socket_data[i], &received_message))
            {
                if (received_message.message_type != MSG_KEEPALIVE && received_message.message_type != NO_MESSAGE)
                {
                    printf("Message received from tcp client: %d, message_type: %d\n", received_message.client, received_message.message_type);
                    if (xQueueSend(server_data->receive_queue, (void *)&received_message, 10) != pdTRUE) {
                        printf("\nUnable to put message on receive_queue\n");
                    }
                }
            }
        }
    }
}

void server_loop(socket_data_t* socket_info)
{
    long ret = 0;
    uint16_t size = 0;

    switch(getSn_SR(socket_info->socket_id))
    {
        case SOCK_ESTABLISHED :
            {
                if (!socket_info->socket_open)
                {
                    socket_info->socket_open = true;
                    socket_info->last_command_received = time_us_64();
                    socket_info->last_command_send = time_us_64();

                    printf("[%d]: SOCK_ESTABLISHED\n",socket_info->socket_id);
                }
            }

            if((size = getSn_RX_RSR(socket_info->socket_id)) > 0) // Don't need to check SOCKERR_BUSY because it doesn't not occur.
            {

                memset(socket_info->receive_buffer + socket_info->receive_size, 0, BUFFER_SIZE);

                ret = recv(socket_info->socket_id, socket_info->receive_buffer, size);
                if(ret != size)
                {
                    printf("[%d]: Received size is not equal to read size. Closing socket.\n",socket_info->socket_id);
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
                    socket_info->receive_size += size;
                    socket_info->last_command_received = time_us_64();
                }
            }
            else
            {
                uint64_t now = time_us_64();
                uint64_t last_command_received_time = (now - socket_info->last_command_received);

                if (last_command_received_time  > (TIMEOUT_SECONDS * 1000 * 1000))
                {
                    //Close the connection when no data received in the last 15 seconds
                    printf("[%d]: Closing due to timeout.\n",socket_info->socket_id);
                    socket_info->socket_open = false;
                    close(socket_info->socket_id);
                    return;
                }
            }

            if (socket_info->send_size > 0)
            {
                ret = send(socket_info->socket_id, socket_info->send_buffer, socket_info->send_size);
                socket_info->send_size = 0;
                socket_info->last_command_send = time_us_64();

                if(ret < 0)
                {
                    socket_info->socket_open = false;
                    close(socket_info->socket_id);
                    return;
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
            socket_info->send_size = 0;
            socket_info->receive_size = 0;

            if( (ret = listen(socket_info->socket_id)) != SOCK_OK)
            {
                return;
            }

            break;

        default :
            break;
    }
}

bool handle_receive_bufffer(socket_data_t* socket_info, message_t* message)
{
    char *end  = strnstr((char*)socket_info->receive_buffer , "#", socket_info->receive_size);
    if (end)
    {
        *end = 0;
        if (!strcmp((char*)socket_info->receive_buffer, "GET"))
        {
            message->message_type = MSG_GET_STATUS;
            message->client = socket_info->socket_id;
            message->value = 0;

            socket_info->receive_size = 0;
            return true;
        }
        if (!strcmp((char*)socket_info->receive_buffer, "SET0"))
        {
            message->message_type = MSG_SET_SPEED;
            message->client = socket_info->socket_id;
            message->value = 0;

            socket_info->receive_size = 0;
            return true;
        }
        if (!strcmp((char*)socket_info->receive_buffer, "SET1"))
        {
            message->message_type = MSG_SET_SPEED;
            message->client = socket_info->socket_id;
            message->value = 1;

            socket_info->receive_size = 0;
            return true;
        }
        if (!strcmp((char*)socket_info->receive_buffer, "SET2"))
        {
            message->message_type = MSG_SET_SPEED;
            message->client = socket_info->socket_id;
            message->value = 2;

            socket_info->receive_size = 0;
            return true;
        }
        if (!strcmp((char*)socket_info->receive_buffer, "SET3"))
        {
            message->message_type = MSG_SET_SPEED;
            message->client = socket_info->socket_id;
            message->value = 3;

            socket_info->receive_size = 0;
            return true;
        }
        if (!strcmp((char*)socket_info->receive_buffer, "HB"))
        {
            message->message_type = MSG_KEEPALIVE;
            message->client = socket_info->socket_id;
            message->value = 0;

            socket_info->receive_size = 0;
            return true;
        }

        //We received a end character, but did not recognise the type; reset the buffer
        socket_info->receive_size = 0;
    }

    return false;
}