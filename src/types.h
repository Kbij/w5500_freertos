#ifndef B0B500A7_6A18_4F4B_9AF0_44F78775ED2E
#define B0B500A7_6A18_4F4B_9AF0_44F78775ED2E
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#define MAX_CONN             5
#define MAX_QUEUE_LENGTH     10
#define BUFFER_SIZE          128

#define NO_MESSAGE           0
#define MSG_GET_STATUS       1
#define MSG_CURRENT_SPEEED   2
#define MSG_SET_SPEED        3
#define MSG_REMAINING_TIME   4
#define MSG_KEEPALIVE        5

typedef struct server_data_t
{
  SemaphoreHandle_t ip_assigned_sem;
  bool server_run;
  QueueHandle_t receive_queue;
  QueueHandle_t send_queue;
  QueueHandle_t blink_queue;
} server_data_t;

typedef struct message_t {
  int client;
  int value;
  int message_type;
} message_t;

#endif /* B0B500A7_6A18_4F4B_9AF0_44F78775ED2E */
