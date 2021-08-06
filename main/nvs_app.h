#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "nvs_app.h"
#include <stdint.h>
typedef struct _parameter   //掉电不丢失参数，//对应mbdata0-63
{
    char ssid[32];      //wifi名称
    char password[32];  //wifi密码

}Parameter;
extern Parameter parameter;

int get_config_param(void);
int set_config_param(void);
int clean_config_param(void);
#ifdef __cplusplus
}
#endif