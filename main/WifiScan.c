#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "string.h"
#include "stdio.h"
#include "nvs_app.h"
#include "WifiScan.h"
/*Set the SSID and Password via "make menuconfig"*/
// #define DEFAULT_SSID CONFIG_WIFI_SSID
// #define DEFAULT_PWD CONFIG_WIFI_PASSWORD

#if CONFIG_WIFI_ALL_CHANNEL_SCAN
#define DEFAULT_SCAN_METHOD WIFI_ALL_CHANNEL_SCAN
#elif CONFIG_WIFI_FAST_SCAN
#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN
#else
#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN
#endif /*CONFIG_SCAN_METHOD*/

#if CONFIG_WIFI_CONNECT_AP_BY_SIGNAL
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#elif CONFIG_WIFI_CONNECT_AP_BY_SECURITY
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SECURITY
#else
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#endif /*CONFIG_SORT_METHOD*/

#if CONFIG_FAST_SCAN_THRESHOLD
#define DEFAULT_RSSI CONFIG_FAST_SCAN_MINIMUM_SIGNAL
#if CONFIG_EXAMPLE_OPEN
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#elif CONFIG_EXAMPLE_WEP
#define DEFAULT_AUTHMODE WIFI_AUTH_WEP
#elif CONFIG_EXAMPLE_WPA
#define DEFAULT_AUTHMODE WIFI_AUTH_WPA_PSK
#elif CONFIG_EXAMPLE_WPA2
#define DEFAULT_AUTHMODE WIFI_AUTH_WPA2_PSK
#else
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#endif
#else
#define DEFAULT_RSSI -127
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#endif /*CONFIG_FAST_SCAN_THRESHOLD*/

static const char *TAG = "scan";
static EventGroupHandle_t wifi_event_group; //定义一个事件的句柄
const int SCAN_DONE_BIT = BIT0;             //定义事件，占用事件变量的第0位，最多可以定义32个事件。
static wifi_scan_config_t scanConf = {
    .ssid = NULL,
    .bssid = NULL,
    .channel = 0,
    .show_hidden = 1}; //定义scanConf结构体，供函数esp_wifi_scan_start调用
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        // ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        ESP_LOGI(TAG, "Got IP: %s\n",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        inet_addr_from_ip4addr(&iaddr, &event->event_info.got_ip.ip_info.ip);
        wifi_connected = 1;
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        ESP_ERROR_CHECK(esp_wifi_connect());
        wifi_connected = 0;
        break;
    case SYSTEM_EVENT_SCAN_DONE:
        xEventGroupSetBits(wifi_event_group, SCAN_DONE_BIT); //设置事件位
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Initialize Wi-Fi as sta and set scan method */
// static void wifi_scan(void)
// {
//     tcpip_adapter_init();
//     ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//     wifi_config_t wifi_config = {
//         .sta = {
//             .ssid = DEFAULT_SSID,
//             .password = DEFAULT_PWD,
//             .scan_method = DEFAULT_SCAN_METHOD,
//             .sort_method = DEFAULT_SORT_METHOD,
//             .threshold.rssi = DEFAULT_RSSI,
//             .threshold.authmode = DEFAULT_AUTHMODE,
//         },
//     };
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());
// }

static void scan_task(void *pvParameters)
{
    while (1)
    {
        xEventGroupWaitBits(wifi_event_group, SCAN_DONE_BIT, 0, 1, portMAX_DELAY); //等待事件被置位，即等待扫描完成
        ESP_LOGI(TAG, "WIFI scan doen");
        xEventGroupClearBits(wifi_event_group, SCAN_DONE_BIT); //清除事件标志位

        uint16_t apCount = 0;
        esp_wifi_scan_get_ap_num(&apCount); //Get number of APs found in last scan
        printf("Number of access points found: %d\n", apCount);
        if (apCount == 0)
        {
            ESP_LOGI(TAG, "Nothing AP found");
            return;
        }                                                                                        //如果apCount没有受到数据，则说明没有路由器
        wifi_ap_record_t *list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount); //定义一个wifi_ap_record_t的结构体的链表空间
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, list));                           //获取上次扫描中找到的AP列表。
        int i;
        for (i = 0; i < apCount; i++)
        {

            printf("(%d,\"%s\",%d,\"" MACSTR " %d\")\r\n", list[i].authmode, list[i].ssid, list[i].rssi,
                   MAC2STR(list[i].bssid), list[i].primary);
            if (strstr((char *)list[i].ssid, "ESPAP") != NULL)
            {
                wifi_config_t wifi_config = {0};
                memset(&wifi_config, 0, sizeof(wifi_config));
                memcpy(wifi_config.sta.ssid, list[i].ssid, strlen((char *)list[i].ssid));
                memcpy(wifi_config.sta.password, "12345678", sizeof("12345678"));
                memset(parameter.ssid, 0, sizeof(parameter.ssid));
                memset(parameter.password, 0, sizeof(parameter.password));
                memcpy(parameter.ssid, list[i].ssid, strlen((char *)list[i].ssid));
                memcpy(parameter.password, "12345678", sizeof("12345678"));
                set_config_param();
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_connect());
                //return;
            }
        }
        free(list);

        // scan again
        vTaskDelay(5000 / portTICK_PERIOD_MS); //调用延时函数，再次扫描
        //The true parameter cause the function to block until the scan is done.
        // ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, 1));//扫描所有可用的AP。
    }
}

void Wifi_Init(int flag)
{
    printf("wifi scan!!!!!!!!!!!!!!!!!!!!!\n");
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();                    //创建一个事件标志组
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL)); //创建事件的任务
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();       //设置默认的wifi栈参数
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                      //初始化WiFi Alloc资源为WiFi驱动，如WiFi控制结构，RX / TX缓冲区，WiFi NVS结构等，此WiFi也启动WiFi任务。
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   // Set the WiFi API configuration storage type
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));         //Set the WiFi operating mode
    ESP_ERROR_CHECK(esp_wifi_start());
    if (flag == 0)
    {
        xTaskCreate(&scan_task, "scan_task", 2048, NULL, 15, NULL); //创建扫描任务
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, 1));         //The true parameter cause the function to block
    }
    else
    {
        wifi_config_t wifi_config = {
            .sta = {
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg = {
                    .capable = true,
                    .required = false},
            },
        };
        memcpy(wifi_config.sta.ssid, parameter.ssid, sizeof(parameter.ssid));
        memcpy(wifi_config.sta.password, parameter.password, sizeof(parameter.password));
        //memcpy(wifi_config.sta.ssid, "ESPAP_68e8", sizeof("ESPAP_68e8"));
        //memcpy(wifi_config.sta.password, "12345678", sizeof("12345678"));
        // memcpy(wifi_config.sta.ssid, "K2P_2_4G", sizeof("K2P_2_4G"));
        // memcpy(wifi_config.sta.password, "378540108", sizeof("378540108"));
        printf("wifi_config.sta.ssid:%s\n", wifi_config.sta.ssid);
        printf("wifi_config.sta.password:%s\n", wifi_config.sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}
void sacn_wifi()
{
    xTaskCreate(&scan_task, "scan_task", 2048, NULL, 15, NULL); //创建扫描任务
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, 1));         //The true parameter cause the function to block
}