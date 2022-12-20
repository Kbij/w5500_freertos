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
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#include "port_common.h"

#include "wizchip_conf.h"
#include "w5x00_spi.h"

#include "dhcp.h"
//#include "dns.h"
#include "socket.h"
#include "main.h"
#include "server.h"
#include "timer.h"

/**
 * ----------------------------------------------------------------------------------------------------
 * Macros
 * ----------------------------------------------------------------------------------------------------
 */
/* Task */
#define DHCP_TASK_STACK_SIZE 2048
#define DHCP_TASK_PRIORITY 8

#define DNS_TASK_STACK_SIZE 512
#define DNS_TASK_PRIORITY 10

/* Clock */
#define PLL_SYS_KHZ (133 * 1000)

/* Buffer */
#define ETHERNET_BUF_MAX_SIZE (1024 * 2)

/* Socket */
#define SOCKET_DHCP 0
//#define SOCKET_DNS 3

/* Retry count */
#define DHCP_RETRY_COUNT 5
#define DNS_RETRY_COUNT 5

/**
 * ----------------------------------------------------------------------------------------------------
 * Variables
 * ----------------------------------------------------------------------------------------------------
 */
/* Network */
static wiz_NetInfo g_net_info =
    {
        .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56}, // MAC address
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


/* Semaphore */
static xSemaphoreHandle dns_sem = NULL;

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

    printf("Started ....\n");
    wizchip_spi_initialize();
    wizchip_cris_initialize();

    wizchip_reset();
    wizchip_initialize();
    wizchip_check();

    wizchip_1ms_timer_initialize(repeating_timer_callback);

    printf("Creating task ....\n");
    xTaskCreate(dhcp_task, "DHCP_Task", DHCP_TASK_STACK_SIZE, NULL, DHCP_TASK_PRIORITY, NULL);
    xTaskCreate(dns_task, "DNS_Task", DNS_TASK_STACK_SIZE, NULL, DNS_TASK_PRIORITY, NULL);

    dns_sem = xSemaphoreCreateCounting((unsigned portBASE_TYPE)0x7fffffff, (unsigned portBASE_TYPE)0);

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
void dhcp_task(void *argument)
{
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

             //   xSemaphoreGive(dns_sem);
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

static uint8_t g_server_buf[ETHERNET_BUF_MAX_SIZE] = {
    0,
};

void dns_task(void *argument)
{
    long ret = 0;
    uint16_t size = 0;
    //uint8_t[100] recvBuf;
    printf(" TCP server waiting for Semaphore...\n");
  //  xSemaphoreTake(dns_sem, portMAX_DELAY);
    socket(SERVER_SOCK_1, Sn_MR_TCP, LISTENING_PORT, 0x0);

    while (1)
    {

        switch(getSn_SR(SERVER_SOCK_1))
        {
            case SOCK_ESTABLISHED :
                {
                   // printf("SOCK_ESTABLISHED\n");
                    // char* welcomeString = "Welcome\n";
                    // ret = send(SERVER_SOCK_1, (uint8_t *)welcomeString, strlen((const char *)welcomeString));

                    // if(ret < 0)
                    // {
                    //     close(SERVER_SOCK_1);
                    //     return;//ret;
                    // }
                }

                if((size = getSn_RX_RSR(SERVER_SOCK_1)) > 0) // Don't need to check SOCKERR_BUSY because it doesn't not occur.
                {

                    memset(g_server_buf, 0, ETHERNET_BUF_MAX_SIZE);

                    ret = recv(SERVER_SOCK_1, g_server_buf, size);
                    //printf("size: %d, ret: %d", size, ret);
                    g_server_buf[ret] = '\0';
                    if(ret != size)
                    {
                        if(ret==SOCK_BUSY) return;// 0;
                        if(ret < 0)
                        {
                            close(SERVER_SOCK_1);
                            return;// ret;
                        }
                    }
                    else
                    {
                        printf("%s", g_server_buf);
                    }
                }
                break;

            case SOCK_CLOSE_WAIT :
                printf("SOCK_CLOSE_WAIT\n");
                if((ret=disconnect(SERVER_SOCK_1)) != SOCK_OK) return;// ret;
                break;

            case SOCK_CLOSED :
                printf("SOCK_CLOSED\n");
                if((ret=socket(SERVER_SOCK_1, Sn_MR_TCP, LISTENING_PORT, 0x0)) != SERVER_SOCK_1)
                {
                    close(SERVER_SOCK_1);
                    return;// ret;
                }
                break;

            case SOCK_INIT :
                printf("SOCK_INIT\n");
                if( (ret = listen(SERVER_SOCK_1)) != SOCK_OK)
                {
                    return;// ret;
                }
                printf("%d:Listen ok\r\n",SERVER_SOCK_1);

                break;

            default :
                break;
        }


    }

    printf(" Tcp Server task exit...\n");
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
    //    DNS_time_handler();
    }
}