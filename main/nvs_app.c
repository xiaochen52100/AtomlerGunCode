#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_app.h"


int get_config_param(void)
{  
    esp_err_t err;
    nvs_handle_t config_handle;   
    size_t len;
 
    err = nvs_open("deviceParameter", NVS_READWRITE, &config_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        //set_config_param();
        return -1;
    } 
    else 
    {
        printf("opening NVS handle Done\n");
        // Read
        printf("Get parameter and key from NVS ... \n");
        //get parameter
        len = sizeof(parameter);
        err = nvs_get_blob(config_handle, "parameter", &parameter, &len);
        if(err==ESP_OK)
        {
            printf("Get parameter success!\n");
            printf("ssid:%s   password:%s\n",parameter.ssid,parameter.password);
            printf("parameter_len=%d\n",len);
            if (strcmp(parameter.ssid,"null")==0)
            {
                printf("parameter is null\n");
                return -1;
            }
            
        }
        else
        {
            return -1;
           printf("get err =0x%x\n",err);
           printf("Get parameter fail!\n");
        }
        err = nvs_commit(config_handle);
        printf((err != ESP_OK) ? "nvs_commit Failed!\n" : " nvs_commit Done\n");
    }
    
    // Close
    nvs_close(config_handle);
    return 0;
}
 
int set_config_param(void)
{
    esp_err_t err;
    nvs_handle_t config_handle;  
    
    err = nvs_open("deviceParameter", NVS_READWRITE, &config_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return -1;
    } 
    else 
    {
        printf("opening NVS handle Done\n");
        printf("Set parameter from NVS ... \n");
        //write parameter
        err=nvs_set_blob(config_handle,"parameter",&parameter,sizeof(parameter));
        if(err==ESP_OK)
            printf("set parameter success!\n");
        else
        {
            printf("set parameter fail!\n");
        }   
        err = nvs_commit(config_handle);
        printf((err != ESP_OK) ? "nvs_commit Failed!\n" : "nvs_commit Done\n");
    }
     // Close
    nvs_close(config_handle);
    printf("--------------------------------------\n");
    return 0;
}
int clean_config_param(void)
{
    esp_err_t err;
    nvs_handle_t config_handle;  
    
    err = nvs_open("deviceParameter", NVS_READWRITE, &config_handle);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return -1;
    } 
    else 
    {
        printf("opening NVS handle Done\n");
        printf("Set parameter from NVS ... \n");
        //write parameter
        memcpy(parameter.ssid,"null",sizeof("null"));
        memcpy(parameter.password,"null",sizeof("null"));
        err=nvs_set_blob(config_handle,"parameter",&parameter,sizeof(parameter));
        if(err==ESP_OK)
            printf("set parameter success!\n");
        else
        {
            printf("set parameter fail!\n");
        }   
        err = nvs_commit(config_handle);
        printf((err != ESP_OK) ? "nvs_commit Failed!\n" : "nvs_commit Done\n");
    }
     // Close
    nvs_close(config_handle);
    printf("--------------------------------------\n");
    return 0;
}