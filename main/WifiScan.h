#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Wifi_Init(int);
void sacn_wifi();
extern Parameter parameter;
extern volatile int wifi_connected;
extern struct in_addr iaddr;
#ifdef __cplusplus
}
#endif