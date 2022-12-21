#include "ventcontrol.h"

#include "types.h"
//#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


#define REL_1 28

void ventcontrol_task(void *params)
{
    server_data_t* server_data = (server_data_t*) params;
    printf("Ventcontrol task started.\n");

    int current_speed = 1;
    // gpio_init(REL_1);
    // gpio_set_dir(REL_1, GPIO_OUT);

    while (true) {
        message_t message;
        if (xQueueReceive(server_data->receive_queue, (void *)&message, (TickType_t) 1000) == pdTRUE)
        {
            printf("Processing message received from client: %d, type: %d\n", message.client, message.message_type);

            if (message.message_type == MSG_SET_SPEED)
            {
                current_speed = message.value;

                if (current_speed == 1)
                {
//                    gpio_put(REL_1, 1);
                }
                else
                {
//                    gpio_put(REL_1, 0);
                }

                // //Blink the led in a different task
                // int blink_time = 200;
                // xQueueSend(queues.blink_queue, (void *)&blink_time, 10);
            }

            message_t reply_message;
            reply_message.client = message.client;
            reply_message.message_type = MSG_CURRENT_SPEEED;
            reply_message.value = current_speed;

            xQueueSend(server_data->send_queue, (void *)&reply_message, 10);

        }
    }
}