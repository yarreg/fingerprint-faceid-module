#ifndef WIFI_H
#define WIFI_H


void wifi_init(void);
void wifi_init_softap(const char *ap_ssid, const char *ap_password);
void wifi_init_sta(const char *sta_ssid, const char *sta_password);
void wifi_start(void);


#endif // WIFI_H
