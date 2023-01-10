/**
 * Copyright (c) 2022 WIZnet Co.,Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * ----------------------------------------------------------------------------------------------------
 * Includes
 * ----------------------------------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include <string.h>
#include "pico/unique_id.h"
#include "server.h"
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "queue.h"

#include "port_common.h"

#include "wizchip_conf.h"
#include "w5x00_spi.h"

#include "dhcp.h"
#include "main.h"
#include "ventcontrol.h"
#include "types.h"
#include "timer.h"

/**
 * ----------------------------------------------------------------------------------------------------
 * Macros
 * ----------------------------------------------------------------------------------------------------
 */
/* Task */
#define DHCP_TASK_STACK_SIZE 2048
#define DHCP_TASK_PRIORITY 8

#define SERVER_TASK_STACK_SIZE 2048
#define SERVER_TASK_PRIORITY 4

/* Clock */
#define PLL_SYS_KHZ (133 * 1000)

/* Buffer */
#define ETHERNET_BUF_MAX_SIZE (1024 * 2)

/* Socket */
#define SOCKET_DHCP 0

/* Retry count */
#define DHCP_RETRY_COUNT 5

/**
 * ----------------------------------------------------------------------------------------------------
 * Variables
 * ----------------------------------------------------------------------------------------------------
 */
/* Network */
static wiz_NetInfo g_net_info =
    {
        .mac = {0x12, 0x00, 0x00, 0x00, 0x00, 0x00}, // MAC address; 0x12 = free range mac
        .ip = {192, 168, 11, 2},                     // IP address
        .sn = {255, 255, 255, 0},                    // Subnet Mask
        .gw = {192, 168, 11, 1},                     // Gateway
        .dns = {8, 8, 8, 8},                         // DNS server
        .dhcp = NETINFO_DHCP                         // DHCP enable/disable
};

static uint8_t g_ethernet_buf[ETHERNET_BUF_MAX_SIZE] = {
    0,
};

/* DHCP */
static uint8_t g_dhcp_get_ip_flag = 0;

/* Server data */
static server_data_t server_data;

/* Timer  */
static volatile uint32_t g_msec_cnt = 0;

/**
 * ----------------------------------------------------------------------------------------------------
 * Functions
 * ----------------------------------------------------------------------------------------------------
 */
/* Task */
void dhcp_task(void *argument);
void dns_task(void *argument);

/* Clock */
static void set_clock_khz(void);

/* DHCP */
static void wizchip_dhcp_init(void);
static void wizchip_dhcp_assign(void);
static void wizchip_dhcp_conflict(void);

/* Timer  */
static void repeating_timer_callback(void);

/**
 * ----------------------------------------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------------------------------------
 */
int main()
{
    /* Initialize */
    set_clock_khz();

    stdio_init_all();

    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    printf("board_id: ");
    for(int i= 0; i<PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
    {
        printf("%X", board_id.id[i]);
    }
    for (int i = 1; i < 5; i++)
    {
        g_net_info.mac[i] = board_id.id[i - 1];
    }

    printf("\nStarted, waiting for phy ....\n");
    wizchip_spi_initialize();
    wizchip_cris_initialize();

    wizchip_reset();
    wizchip_initialize();
    wizchip_check();
    setSHAR(g_net_info.mac);
    dhcpHostName("ventcontrol");

    wizchip_1ms_timer_initialize(repeating_timer_callback);
    server_data.ip_assigned_sem = xSemaphoreCreateCounting((unsigned portBASE_TYPE)0x7fffffff, (unsigned portBASE_TYPE)0);
    server_data.server_run = false;
    server_data.receive_queue = xQueueCreate(MAX_QUEUE_LENGTH, sizeof(message_t));
    server_data.send_queue = xQueueCreate(MAX_QUEUE_LENGTH, sizeof(message_t));
    server_data.blink_queue = xQueueCreate(MAX_QUEUE_LENGTH, sizeof(int));

    printf("Creating task ....\n");
    xTaskCreate(dhcp_task, "DHCP_Task", DHCP_TASK_STACK_SIZE, &server_data, DHCP_TASK_PRIORITY, NULL);
    xTaskCreate(server_task, "Server_TASK", SERVER_TASK_STACK_SIZE, &server_data, SERVER_TASK_PRIORITY, NULL);
    xTaskCreate(ventcontrol_task, "Ventcontrol_TASK", SERVER_TASK_STACK_SIZE, &server_data, SERVER_TASK_PRIORITY, NULL);

    vTaskStartScheduler();

    while (1)
    {
        ;
    }
}

/**
 * ----------------------------------------------------------------------------------------------------
 * Functions
 * ----------------------------------------------------------------------------------------------------
 */
/* Task */
void dhcp_task(void *params)
{
    server_data_t* server_data = (server_data_t*) params;
    printf("DHCP task ....\n");
    int retval = 0;
    uint8_t link;
    //uint16_t len = 0;
    uint32_t dhcp_retry = 0;

    if (g_net_info.dhcp == NETINFO_DHCP) // DHCP
    {
        wizchip_dhcp_init();
    }
    else // static
    {
        network_initialize(g_net_info);

        /* Get network information */
        print_network_information(g_net_info);

        while (1)
        {
            vTaskDelay(1000 * 1000);
        }
    }

    while (1)
    {
        link = wizphy_getphylink();

        if (link == PHY_LINK_OFF)
        {
            printf("PHY_LINK_OFF\n");
            server_data->server_run = false;

            DHCP_stop();

            while (1)
            {
                link = wizphy_getphylink();

                if (link == PHY_LINK_ON)
                {
                    wizchip_dhcp_init();

                    dhcp_retry = 0;

                    break;
                }

                vTaskDelay(1000);
            }
        }

        retval = DHCP_run();

        if (retval == DHCP_IP_LEASED)
        {
            if (g_dhcp_get_ip_flag == 0)
            {
                dhcp_retry = 0;

                printf(" DHCP success\n");

                g_dhcp_get_ip_flag = 1;

                server_data->server_run = true;
                xSemaphoreGive(server_data->ip_assigned_sem);
            }
        }
        else if (retval == DHCP_FAILED)
        {
            g_dhcp_get_ip_flag = 0;
            dhcp_retry++;

            if (dhcp_retry <= DHCP_RETRY_COUNT)
            {
                printf(" DHCP timeout occurred and retry %d\n", dhcp_retry);
            }
        }

        if (dhcp_retry > DHCP_RETRY_COUNT)
        {
            printf(" DHCP failed\n");

            DHCP_stop();

            while (1)
            {
                vTaskDelay(1000 * 1000);
            }
        }

        vTaskDelay(10);
    }
}

/* Clock */
static void set_clock_khz(void)
{
    // set a system clock frequency in khz
    set_sys_clock_khz(PLL_SYS_KHZ, true);

    // configure the specified clock
    clock_configure(
        clk_peri,
        0,                                                // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
        PLL_SYS_KHZ * 1000,                               // Input frequency
        PLL_SYS_KHZ * 1000                                // Output (must be same as no divider)
    );
}

/* DHCP */
static void wizchip_dhcp_init(void)
{
    printf(" DHCP client running\n");

    DHCP_init(SOCKET_DHCP, g_ethernet_buf);

    reg_dhcp_cbfunc(wizchip_dhcp_assign, wizchip_dhcp_assign, wizchip_dhcp_conflict);

    g_dhcp_get_ip_flag = 0;
}

static void wizchip_dhcp_assign(void)
{
    getIPfromDHCP(g_net_info.ip);
    getGWfromDHCP(g_net_info.gw);
    getSNfromDHCP(g_net_info.sn);
    getDNSfromDHCP(g_net_info.dns);

    g_net_info.dhcp = NETINFO_DHCP;

    /* Network initialize */
    network_initialize(g_net_info); // apply from DHCP

    print_network_information(g_net_info);
    printf(" DHCP leased time : %ld seconds\n", getDHCPLeasetime());
}

static void wizchip_dhcp_conflict(void)
{
    printf(" Conflict IP from DHCP\n");

    // halt or reset or any...
    while (1)
    {
        vTaskDelay(1000 * 1000);
    }
}

/* Timer */
static void repeating_timer_callback(void)
{
    g_msec_cnt++;

    if (g_msec_cnt >= 1000 - 1)
    {
        g_msec_cnt = 0;

        DHCP_time_handler();
    }
}
