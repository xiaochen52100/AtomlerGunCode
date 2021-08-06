#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "driver/uart.h"
#include "driver/gpio.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/semphr.h"
#include "nvs_app.h"
#include "WifiScan.h"
#include "station_app.h"

#define UDP_PORT CONFIG_EXAMPLE_PORT

#define MULTICAST_LOOPBACK CONFIG_EXAMPLE_LOOPBACK

#define MULTICAST_TTL CONFIG_EXAMPLE_MULTICAST_TTL

#define MULTICAST_IPV4_ADDR CONFIG_EXAMPLE_MULTICAST_IPV4_ADDR
#define MULTICAST_IPV6_ADDR CONFIG_EXAMPLE_MULTICAST_IPV6_ADDR

#define LISTEN_ALL_IF EXAMPLE_MULTICAST_LISTEN_ALL_IF

static const char *TAG = "multicast";
#ifdef CONFIG_EXAMPLE_IPV4
static const char *V4TAG = "mcast-ipv4";
#endif
#ifdef CONFIG_EXAMPLE_IPV6
static const char *V6TAG = "mcast-ipv6";
#endif
Parameter parameter = {
    .ssid = "K2P_2_4G",
    .password = "378540108"};
int sock;
struct in_addr iaddr = {0};
static void mcast_send_data(char *data);
int btn_flag = 0;
int btn_flag2 = 0;
int btn_state = 0;
volatile int wifi_connected=0;
SemaphoreHandle_t xSemaphore = NULL;
double oldProgess = 999;
int oldLastTime = 999;
int sendData(const char *logName, const char *data);

#define DEFAULT_VREF 1100                           //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES 64                            //Multisampling
static const adc_channel_t channel = ADC_CHANNEL_5; //GPIO33 if ADC1
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static esp_adc_cal_characteristics_t *adc_chars;
static const adc_atten_t atten = ADC_ATTEN_DB_0;
static const adc_unit_t unit = ADC_UNIT_1;
/********串口操作*************/
static const int RX_BUF_SIZE = 1024;
static const char END[] = {0xff, 0xff, 0xff};
#define TXD_PIN (GPIO_NUM_26)
#define RXD_PIN (GPIO_NUM_27)

static uint8_t wifi_state = 0;
static uint8_t wifi_state_old = 0;
struct device_state_t
{
    int temperature;
    int humidity;
    int level;
    int state;
    int taskTime1;
    int lastTime1;
    int gear1;
    double progess1;
    int taskTime2;
    int lastTime2;
    double progess2;
    int taskTime3;
    int lastTime3;
    double progess3;
    int taskTime4;
    int lastTime4;
    double progess4;
};
struct device_state_t device_state;

static void check_efuse(void)
{
    //Check if TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK)
    {
        printf("eFuse Two Point: Supported\n");
    }
    else
    {
        printf("eFuse Two Point: NOT supported\n");
    }
    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK)
    {
        printf("eFuse Vref: Supported\n");
    }
    else
    {
        printf("eFuse Vref: NOT supported\n");
    }
}

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
    {
        printf("Characterized using Two Point Value\n");
    }
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
    {
        printf("Characterized using eFuse Vref\n");
    }
    else
    {
        printf("Characterized using Default Vref\n");
    }
}

int recv_uart_data = 0;

void uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}
int sendData(const char *logName, const char *data)
{
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE)
    {
        const int len = strlen(data);
        const int txBytes = uart_write_bytes(UART_NUM_1, data, len);
        //ESP_LOGI(logName, "Wrote %s", data);
        xSemaphoreGive(xSemaphore);
        return txBytes;
    }
    return -1;
}
static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t *data = (uint8_t *)malloc(RX_BUF_SIZE + 1);
    while (1)
    {
        memset(data, 0, RX_BUF_SIZE + 1);
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 50 / portTICK_RATE_MS);
        if (rxBytes == 2)
        {
            if (data[0] == 0xb1 && data[1] == 0xb2)
            {
                clean_config_param();
            }
        }

        if (rxBytes == 4)
        {
            if (data[0] == 0x1f) //弱
            {
                char send_test[5] = {2, 0, 0, 0, 0};
                send_test[4] = data[0];
                mcast_send_data(send_test);
                // const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 1000 / portTICK_RATE_MS);
                // mcast_send_data(send_test);
                device_state.gear1 = 1;
            }
            else if (data[0] == 0x20) //强
            {
                char send_test[5] = {2, 0, 0, 0, 0};
                send_test[4] = data[0];
                mcast_send_data(send_test);
                // const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 1000 / portTICK_RATE_MS);
                // mcast_send_data(send_test);
                device_state.gear1 = 2;
            }
            else
            {
                if (btn_flag == 1)
                {
                    ESP_LOGI(RX_TASK_TAG, "btn_flag\n");
                    btn_flag = 0;
                    continue;
                }
                if (btn_state == 1) //停止
                {
                    ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%d'", rxBytes, data[0]);
                    char send_test[5] = {1, 0, 0, 0, 0};
                    mcast_send_data(send_test);
                    btn_flag2 = 1;
                    btn_state = 0;
                }
                else if (btn_state == 0 && data[1] == 0 && data[1] == 0) //开始
                {
                    char send_test[5] = {1};
                    ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%d'", rxBytes, data[0]);
                    char tmp = data[0];
                    send_test[4] = tmp;
                    send_test[1] = 0;
                    send_test[2] = 0;
                    send_test[3] = 0;
                    mcast_send_data(send_test);
                    btn_flag2 = 1;
                    btn_state = 1;
                    if (device_state.gear1 == 1)
                    {
                        sendData(TAG, "click bt1,1");
                        sendData(TAG, END);
                    }
                    else if (device_state.gear1 == 2)
                    {
                        sendData(TAG, "click bt2,1");
                        sendData(TAG, END);
                    }
                }
            }
        }
    }
    free(data);
}
/********串口操作*************/
#ifdef CONFIG_EXAMPLE_IPV4
/* Add a socket, either IPV4-only or IPV6 dual mode, to the IPV4
   multicast group */
static int socket_add_ipv4_multicast_group(int sock, bool assign_source_if)
{
    struct ip_mreq imreq = {0};
    //struct in_addr iaddr = {0};
    int err = 0;
    // Configure source interface
#if LISTEN_ALL_IF
    imreq.imr_interface.s_addr = IPADDR_ANY;
#else
    // esp_netif_ip_info_t ip_info = {0};
    // err = esp_netif_get_ip_info(get_example_netif(), &ip_info);
    // if (err != ESP_OK)
    // {
    //     ESP_LOGE(V4TAG, "Failed to get IP address info. Error 0x%x", err);
    //     goto err;
    // }
    // inet_addr_from_ip4addr(&iaddr, &ip_info.ip);
#endif // LISTEN_ALL_IF
    // Configure multicast address to listen to
    err = inet_aton(MULTICAST_IPV4_ADDR, &imreq.imr_multiaddr.s_addr);
    if (err != 1)
    {
        ESP_LOGE(V4TAG, "Configured IPV4 multicast address '%s' is invalid.", MULTICAST_IPV4_ADDR);
        // Errors in the return value have to be negative
        err = -1;
        goto err;
    }
    ESP_LOGI(TAG, "Configured IPV4 Multicast address %s", inet_ntoa(imreq.imr_multiaddr.s_addr));
    if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr)))
    {
        ESP_LOGW(V4TAG, "Configured IPV4 multicast address '%s' is not a valid multicast address. This will probably not work.", MULTICAST_IPV4_ADDR);
    }

    if (assign_source_if)
    {
        // Assign the IPv4 multicast source interface, via its IP
        // (only necessary if this socket is IPV4 only)
        err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iaddr,
                         sizeof(struct in_addr));
        if (err < 0)
        {
            ESP_LOGE(V4TAG, "Failed to set IP_MULTICAST_IF. Error %d", errno);
            goto err;
        }
    }

    err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &imreq, sizeof(struct ip_mreq));
    if (err < 0)
    {
        ESP_LOGE(V4TAG, "Failed to set IP_ADD_MEMBERSHIP. Error %d", errno);
        goto err;
    }

err:
    return err;
}
#endif /* CONFIG_EXAMPLE_IPV4 */

#ifdef CONFIG_EXAMPLE_IPV4_ONLY
static int create_multicast_ipv4_socket(void)
{
    struct sockaddr_in saddr = {0};
    int sock = -1;
    int err = 0;

    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(V4TAG, "Failed to create socket. Error %d", errno);
        return -1;
    }

    // Bind the socket to any address
    saddr.sin_family = PF_INET;
    saddr.sin_port = htons(UDP_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
    if (err < 0)
    {
        ESP_LOGE(V4TAG, "Failed to bind socket. Error %d", errno);
        goto err;
    }

    // Assign multicast TTL (set separately from normal interface TTL)
    uint8_t ttl = MULTICAST_TTL;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));
    if (err < 0)
    {
        ESP_LOGE(V4TAG, "Failed to set IP_MULTICAST_TTL. Error %d", errno);
        goto err;
    }

#if MULTICAST_LOOPBACK
    // select whether multicast traffic should be received by this device, too
    // (if setsockopt() is not called, the default is no)
    uint8_t loopback_val = MULTICAST_LOOPBACK;
    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP,
                     &loopback_val, sizeof(uint8_t));
    if (err < 0)
    {
        ESP_LOGE(V4TAG, "Failed to set IP_MULTICAST_LOOP. Error %d", errno);
        goto err;
    }
#endif

    // this is also a listening socket, so add it to the multicast
    // group for listening...
    err = socket_add_ipv4_multicast_group(sock, true);
    if (err < 0)
    {
        goto err;
    }

    // All set, socket is configured for sending and receiving
    return sock;

err:
    close(sock);
    return -1;
}
#endif /* CONFIG_EXAMPLE_IPV4_ONLY */

#ifdef CONFIG_EXAMPLE_IPV6
static int create_multicast_ipv6_socket(void)
{
    struct sockaddr_in6 saddr = {0};
    int netif_index;
    struct in6_addr if_inaddr = {0};
    struct ip6_addr if_ipaddr = {0};
    struct ipv6_mreq v6imreq = {0};
    int sock = -1;
    int err = 0;

    sock = socket(PF_INET6, SOCK_DGRAM, IPPROTO_IPV6);
    if (sock < 0)
    {
        ESP_LOGE(V6TAG, "Failed to create socket. Error %d", errno);
        return -1;
    }

    // Bind the socket to any address
    saddr.sin6_family = AF_INET6;
    saddr.sin6_port = htons(UDP_PORT);
    bzero(&saddr.sin6_addr.un, sizeof(saddr.sin6_addr.un));
    err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in6));
    if (err < 0)
    {
        ESP_LOGE(V6TAG, "Failed to bind socket. Error %d", errno);
        goto err;
    }

    // Selct the interface to use as multicast source for this socket.
#if LISTEN_ALL_IF
    bzero(&if_inaddr.un, sizeof(if_inaddr.un));
#else
    // Read interface adapter link-local address and use it
    // to bind the multicast IF to this socket.
    //
    // (Note the interface may have other non-LL IPV6 addresses as well,
    // but it doesn't matter in this context as the address is only
    // used to identify the interface.)
    err = esp_netif_get_ip6_linklocal(EXAMPLE_INTERFACE, (esp_ip6_addr_t *)&if_ipaddr);
    inet6_addr_from_ip6addr(&if_inaddr, &if_ipaddr);
    if (err != ESP_OK)
    {
        ESP_LOGE(V6TAG, "Failed to get IPV6 LL address. Error 0x%x", err);
        goto err;
    }
#endif // LISTEN_ALL_IF

    // search for netif index
    netif_index = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
    if (netif_index < 0)
    {
        ESP_LOGE(V6TAG, "Failed to get netif index");
        goto err;
    }
    // Assign the multicast source interface, via its IP
    err = setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF, &netif_index, sizeof(uint8_t));
    if (err < 0)
    {
        ESP_LOGE(V6TAG, "Failed to set IPV6_MULTICAST_IF. Error %d", errno);
        goto err;
    }

    // Assign multicast TTL (set separately from normal interface TTL)
    uint8_t ttl = MULTICAST_TTL;
    setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof(uint8_t));
    if (err < 0)
    {
        ESP_LOGE(V6TAG, "Failed to set IPV6_MULTICAST_HOPS. Error %d", errno);
        goto err;
    }

#if MULTICAST_LOOPBACK
    // select whether multicast traffic should be received by this device, too
    // (if setsockopt() is not called, the default is no)
    uint8_t loopback_val = MULTICAST_LOOPBACK;
    err = setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
                     &loopback_val, sizeof(uint8_t));
    if (err < 0)
    {
        ESP_LOGE(V6TAG, "Failed to set IPV6_MULTICAST_LOOP. Error %d", errno);
        goto err;
    }
#endif

    // this is also a listening socket, so add it to the multicast
    // group for listening...
#ifdef CONFIG_EXAMPLE_IPV6
    // Configure multicast address to listen to
    err = inet6_aton(MULTICAST_IPV6_ADDR, &v6imreq.ipv6mr_multiaddr);
    if (err != 1)
    {
        ESP_LOGE(V6TAG, "Configured IPV6 multicast address '%s' is invalid.", MULTICAST_IPV6_ADDR);
        goto err;
    }
    ESP_LOGI(TAG, "Configured IPV6 Multicast address %s", inet6_ntoa(v6imreq.ipv6mr_multiaddr));
    ip6_addr_t multi_addr;
    inet6_addr_to_ip6addr(&multi_addr, &v6imreq.ipv6mr_multiaddr);
    if (!ip6_addr_ismulticast(&multi_addr))
    {
        ESP_LOGW(V6TAG, "Configured IPV6 multicast address '%s' is not a valid multicast address. This will probably not work.", MULTICAST_IPV6_ADDR);
    }
    // Configure source interface
    v6imreq.ipv6mr_interface = (unsigned int)netif_index;
    err = setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                     &v6imreq, sizeof(struct ipv6_mreq));
    if (err < 0)
    {
        ESP_LOGE(V6TAG, "Failed to set IPV6_ADD_MEMBERSHIP. Error %d", errno);
        goto err;
    }
#endif

#if CONFIG_EXAMPLE_IPV4_V6
    // Add the common IPV4 config options
    err = socket_add_ipv4_multicast_group(sock, false);
    if (err < 0)
    {
        goto err;
    }
#endif

#if CONFIG_EXAMPLE_IPV4_V6
    int only = 0;
#else
    int only = 1; /* IPV6-only socket */
#endif
    err = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof(int));
    if (err < 0)
    {
        ESP_LOGE(V6TAG, "Failed to set IPV6_V6ONLY. Error %d", errno);
        goto err;
    }
    ESP_LOGI(TAG, "Socket set IPV6-only");

    // All set, socket is configured for sending and receiving
    return sock;

err:
    close(sock);
    return -1;
}
#endif
static void mcast_send_data(char *data)
{
    int err = 0;
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
    }

    if (sock < 0)
    {
        // Nothing to do!
        vTaskDelay(5 / portTICK_PERIOD_MS);
        return;
    }
    // set destination multicast addresses for sending from these sockets
    struct sockaddr_in sdestv4 = {
        .sin_family = PF_INET,
        .sin_port = htons(UDP_PORT),
    };
    // We know this inet_aton will pass because we did it above already
    inet_aton(MULTICAST_IPV4_ADDR, &sdestv4.sin_addr.s_addr);
    static int send_count;
    char sendbuf[48];
    char addrbuf[32] = {0};

    struct addrinfo hints = {
        .ai_flags = AI_PASSIVE,
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *res;
    hints.ai_family = AF_INET; // For an IPv4 socket
    err = getaddrinfo(CONFIG_EXAMPLE_MULTICAST_IPV4_ADDR,
                      NULL,
                      &hints,
                      &res);
    if (err < 0)
    {
        ESP_LOGE(TAG, "getaddrinfo() failed for IPV4 destination address. error: %d", err);
        return;
    }
    if (res == 0)
    {
        ESP_LOGE(TAG, "getaddrinfo() did not return any addresses");
        return;
    }
    ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(UDP_PORT);
    inet_ntoa_r(((struct sockaddr_in *)res->ai_addr)->sin_addr, addrbuf, sizeof(addrbuf) - 1);

    err = sendto(sock, data, 5, 0, res->ai_addr, res->ai_addrlen);
    ESP_LOGI(TAG, "Sending to IPV4 multicast address %s:%d...%s\n", addrbuf, UDP_PORT, data);
    freeaddrinfo(res);
    if (err < 0)
    {
        ESP_LOGE(TAG, "IPV4 sendto failed. errno: %d", errno);
        return;
    }
}
static void mcast_example_task(void *pvParameters)
{
    //char send_test[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    while (1)
    {
#ifdef CONFIG_EXAMPLE_IPV4_ONLY
        sock = create_multicast_ipv4_socket();
        if (sock < 0)
        {
            wifi_state = 0;
            ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
        }
#else
        sock = create_multicast_ipv6_socket();
        if (sock < 0)
        {
            wifi_state = 0;
            ESP_LOGE(TAG, "Failed to create IPv6 multicast socket");
        }
#endif

        if (sock < 0)
        {
            // Nothing to do!
            wifi_state = 0;
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

#ifdef CONFIG_EXAMPLE_IPV4
        // set destination multicast addresses for sending from these sockets
        struct sockaddr_in sdestv4 = {
            .sin_family = PF_INET,
            .sin_port = htons(UDP_PORT),
        };
        // We know this inet_aton will pass because we did it above already
        inet_aton(MULTICAST_IPV4_ADDR, &sdestv4.sin_addr.s_addr);
#endif

#ifdef CONFIG_EXAMPLE_IPV6
        struct sockaddr_in6 sdestv6 = {
            .sin6_family = PF_INET6,
            .sin6_port = htons(UDP_PORT),
        };
        // We know this inet_aton will pass because we did it above already
        inet6_aton(MULTICAST_IPV6_ADDR, &sdestv6.sin6_addr);
#endif

        // Loop waiting for UDP received, and sending UDP packets if we don't
        // see any.
        int err = 1;
        wifi_state = 0;
        while (err > 0)
        {
            struct timeval tv = {
                .tv_sec = 2,
                .tv_usec = 0,
            };
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);

            int s = select(sock + 1, &rfds, NULL, NULL, &tv);
            if (s < 0)
            {
                ESP_LOGE(TAG, "Select failed: errno %d", errno);
                err = -1;
                wifi_state = 0;
                break;
            }
            else if (s > 0)
            {
                if (FD_ISSET(sock, &rfds))
                {
                    wifi_state = 1;
                    // Incoming datagram received
                    char recvbuf[80];
                    char raddr_name[32] = {0};

                    struct sockaddr_in6 raddr; // Large enough for both IPv4 or IPv6
                    socklen_t socklen = sizeof(raddr);
                    int len = recvfrom(sock, recvbuf, sizeof(recvbuf) - 1, 0,
                                       (struct sockaddr *)&raddr, &socklen);
                    if (len < 0)
                    {
                        ESP_LOGE(TAG, "multicast recvfrom failed: errno %d", errno);
                        err = -1;
                        break;
                    }

                    // Get the sender's address as a string
#ifdef CONFIG_EXAMPLE_IPV4
                    if (raddr.sin6_family == PF_INET)
                    {
                        inet_ntoa_r(((struct sockaddr_in *)&raddr)->sin_addr.s_addr,
                                    raddr_name, sizeof(raddr_name) - 1);
                    }
#endif
#ifdef CONFIG_EXAMPLE_IPV6
                    if (raddr.sin6_family == PF_INET6)
                    {
                        inet6_ntoa_r(raddr.sin6_addr, raddr_name, sizeof(raddr_name) - 1);
                    }
#endif
                    ESP_LOGI(TAG, "received %d bytes from %s:", len, raddr_name);

                    recvbuf[len] = 0; // Null-terminate whatever we received and treat like a string...
                    //ESP_LOGI(TAG, "%s", recvbuf);
                    //接收到主机发来的状态数据
                    device_state.state = recvbuf[31];
                    memcpy(&device_state.progess1, recvbuf + 20, 8);
                    device_state.lastTime1 = recvbuf[19] + (recvbuf[18] << 8) + (recvbuf[17] << 16) + (recvbuf[16] << 24);

                    ESP_LOGI(TAG, "state:%d  progess:%f lastTime:%d \n", device_state.state, device_state.progess1, device_state.lastTime1);
                    if (oldProgess != device_state.progess1)
                    {
                        char progess[32];
                        sprintf(progess, "j0.val=%d", (int)(100 - device_state.progess1 * 100));
                        sendData(TAG, progess);
                        sendData(TAG, END);
                        oldProgess = device_state.progess1;
                    }
                    if (oldLastTime != device_state.lastTime1)
                    {
                        char lasttime[32];
                        sprintf(lasttime, "t0.txt=\"%02d:%02d\"", device_state.lastTime1 / 60, device_state.lastTime1 % 60);
                        sendData(TAG, lasttime);
                        sendData(TAG, END);
                        oldLastTime = device_state.lastTime1;
                        // if (device_state.gear1 == 1)
                        // {
                        //     sendData(TAG, "click bt1,1");
                        //     sendData(TAG, END);
                        // }
                        // else if (device_state.gear1 == 2)
                        // {
                        //     sendData(TAG, "click bt2,1");
                        //     sendData(TAG, END);
                        // }
                    }
                    if (device_state.state == 0)
                    {
                        /* code */
                    }

                    //ESP_LOGI(TAG, "state:%d\n", device_state.state&(1>>0));
                    if (device_state.state == 1 && btn_state == 0 && btn_flag2 == 0) //只执行一次
                    {
                        sendData(TAG, "click bt0,1");
                        sendData(TAG, END);
                        if (device_state.gear1 == 1)
                        {
                            sendData(TAG, "click bt1,1");
                            sendData(TAG, END);
                        }
                        else if (device_state.gear1 == 2)
                        {
                            sendData(TAG, "click bt2,1");
                            sendData(TAG, END);
                        }
                        btn_flag = 1;
                        btn_state = 1;
                    }
                    else if (device_state.state == 0 && btn_state == 1 && btn_flag2 == 0)
                    {
                        sendData(TAG, "click bt0,1");
                        sendData(TAG, END);
                        btn_state = 0;
                        btn_flag = 1;
                    }
                    if (device_state.gear1 != recvbuf[35]) //更新屏幕强弱
                    {
                        device_state.gear1 = recvbuf[35];
                        //ESP_LOGI(TAG, "gear1:%d \n", device_state.gear1);
                        if (device_state.gear1 == 1)
                        {
                            sendData(TAG, "click bt1,1");
                            sendData(TAG, END);
                        }
                        else if (device_state.gear1 == 2)
                        {
                            sendData(TAG, "click bt2,1");
                            sendData(TAG, END);
                        }
                    }

                    if (btn_flag2 == 1)
                    {
                        btn_flag2 = 0;
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "multicast recvfrom failed: select\n");
                    wifi_state = 0;
                }
            }
        }
        wifi_state = 0;
        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(sock, 0);
        close(sock);
    }
}
static void adc_task(void *arg)
{
    uint8_t batteryOld = 450;
    long chargeTime = 0;
    uint8_t electricity = 0;
    uint8_t chargeState = 0;
    uint8_t batteryCount =0;
    gpio_pad_select_gpio(32);
    gpio_set_direction(32, GPIO_MODE_INPUT);
    printf("input charge is %d\n", gpio_get_level(32));
    //初始化ADC
    check_efuse();
    adc1_config_width(width);
    adc1_config_channel_atten(channel, atten);
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);
    while (1)
    {
        if (wifi_state_old != wifi_state)
        {
            if (wifi_state == 0)
            {
                char wifi_string[32]; //更新wifi状态
                sprintf(wifi_string, "p0.pic=%d", 0);
                sendData(TAG, wifi_string);
                sendData(TAG, END);
            }
            else
            {
                char wifi_string[32]; //更新wifi状态
                sprintf(wifi_string, "p0.pic=%d", 1);
                sendData(TAG, wifi_string);
                sendData(TAG, END);
            }
            wifi_state_old = wifi_state;
        }
        if (gpio_get_level(32) == 0)
        {
            //更新电池电量
            uint32_t adc_reading = 0;
            chargeTime=0;
            //Multisampling
            for (int i = 0; i < 64; i++)
            {

                adc_reading += adc1_get_raw((adc1_channel_t)channel);
            }
            adc_reading /= 64;
            //Convert adc_reading to voltage in mV
            uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars); //  350-450
            electricity = voltage - 350;
            batteryOld = electricity;
            if (chargeState == 1)
            {
                char electricity_string[32];
                sprintf(electricity_string, "p2.pic=%d", 7);
                sendData(TAG, electricity_string);
                sendData(TAG, END);
            }
            chargeState = 0;
            batteryCount=0;
        }
        if (gpio_get_level(32) == 1)
        {
            if (chargeState == 0)
            {
                char electricity_string[32];
                sprintf(electricity_string, "p2.pic=%d", 6);
                sendData(TAG, electricity_string);
                sendData(TAG, END);
            }
            chargeState = 1;
            chargeTime++;
            electricity = batteryOld + (chargeTime / 60 / 6)*1.15;
        }

        //更新电池电量
        // uint32_t adc_reading = 0;
        // int8_t electricity = 0;
        // //Multisampling
        // for (int i = 0; i < 64; i++)
        // {

        //     adc_reading += adc1_get_raw((adc1_channel_t)channel);
        // }
        // adc_reading /= 64;
        // //Convert adc_reading to voltage in mV
        // uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars); //  350-450
        // electricity = voltage - 350;
        if (electricity > 100)
        {
            electricity = 100;
        }
        if (electricity < 0)
        {
            electricity = 0;
        }
        //printf("electricity：%d  batteryCount:%d \n", electricity,batteryCount);
        if (electricity > 0 && electricity < 25)
        {
            char electricity_string[32];
            batteryCount=1;
            sprintf(electricity_string, "p1.pic=%d", 2);
            sendData(TAG, electricity_string);
            sendData(TAG, END);
        }
        else if (electricity > 25 && electricity < 50)
        {
            char electricity_string[32];
            batteryCount=2;
            sprintf(electricity_string, "p1.pic=%d", 3);
            sendData(TAG, electricity_string);
            sendData(TAG, END);
        }
        else if (electricity > 50 && electricity < 75)
        {
            char electricity_string[32];
            batteryCount=3;
            sprintf(electricity_string, "p1.pic=%d", 4);
            sendData(TAG, electricity_string);
            sendData(TAG, END);
        }
        else if (electricity > 75 && electricity <= 100)
        {
            char electricity_string[32];
            batteryCount=4;
            sprintf(electricity_string, "p1.pic=%d", 5);
            sendData(TAG, electricity_string);
            sendData(TAG, END);
        }

        //printf("Raw: %d\tVoltage: %dmV  electricity：%d\n", adc_reading, voltage, electricity);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void app_main(void)
{
    int val=0;
    xSemaphore = xSemaphoreCreateMutex(); //初始化互斥锁
    uart_init();
    wifi_state = 0;
    wifi_state_old = 0;
    char wifi_string[32]; //更新wifi状态
    sprintf(wifi_string, "p0.pic=%d", 0);
    sendData(TAG, wifi_string);
    sendData(TAG, END);

    xTaskCreate(adc_task, "adc_task_", 1024 * 2, NULL, configMAX_PRIORITIES, NULL);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    if (get_config_param() == -1) //如果nvs里没有wifi信息
    {
        Wifi_Init(0);
    }
    else
    {
        Wifi_Init(1);
    }
    xTaskCreate(rx_task, "uart_rx_task", 1024 * 2, NULL, configMAX_PRIORITIES, NULL);
    while (wifi_connected!=1)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    xTaskCreate(&mcast_example_task, "mcast_task", 4096, NULL, 5, NULL);
    
}
