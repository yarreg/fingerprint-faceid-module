#include "wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#if IP_NAPT
#include "lwip/lwip_napt.h"
#endif
#include "esp_system.h"  // Added for esp_random()

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_ESP_WIFI_STA_SSID "mywifissid"
*/

/* STA Configuration */
#define ESP_MAXIMUM_RETRY           CONFIG_ESP_MAXIMUM_STA_RETRY

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WAPI_PSK
#endif

/* AP Configuration */
#define WIFI_CHANNEL            CONFIG_ESP_WIFI_AP_CHANNEL
#define MAX_STA_CONN            CONFIG_ESP_MAX_STA_CONN_AP

#define WIFI_IMMEDIATE_CONNECT_RETRY_COUNT 3   // Renamed constant for 3 immediate retries

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG_AP  = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";

static int16_t s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *esp_netif_ap  = NULL;
static esp_netif_t *esp_netif_sta = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" left, AID=%d, reason:%d", MAC2STR(event->mac), event->aid, event->reason);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_STA, "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG_STA, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY || ESP_MAXIMUM_RETRY == -1) {
            s_retry_num++;
            int32_t exponent = s_retry_num - WIFI_IMMEDIATE_CONNECT_RETRY_COUNT;
            if (exponent > 0) {
                int32_t base_delay = (1 << exponent);
                if (base_delay > 10) base_delay = 10;
                uint32_t jitter = esp_random() % 1000; // in ms
                uint32_t delay_ms = base_delay * 1000 + jitter;
                ESP_LOGI(TAG_STA, "Reconnecting to AP in %" PRIu32 "ms (attempt %" PRIi16 ")", delay_ms, s_retry_num);
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            } else {
                ESP_LOGI(TAG_STA, "Reconnecting to AP immediately (attempt %" PRIi16 ")", s_retry_num);
            }
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG_STA, "Connect to the AP failed");
        }
    }
}

void wifi_init_softap(const char *ap_ssid, const char *ap_password) {
    esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            // Use parameters instead of macros
            // .ssid = WIFI_AP_SSID,
            // .ssid_len = strlen(WIFI_AP_SSID),
            // .password = WIFI_AP_PASSWD,
            .channel = WIFI_CHANNEL,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    strncpy((char *)wifi_ap_config.ap.ssid, ap_ssid, sizeof(wifi_ap_config.ap.ssid));
    wifi_ap_config.ap.ssid[sizeof(wifi_ap_config.ap.ssid)-1] = '\0';
    wifi_ap_config.ap.ssid_len = strlen((const char *)wifi_ap_config.ap.ssid);

    strncpy((char *)wifi_ap_config.ap.password, ap_password, sizeof(wifi_ap_config.ap.password));
    wifi_ap_config.ap.password[sizeof(wifi_ap_config.ap.password)-1] = '\0';

    if (strlen(ap_password) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             ap_ssid, ap_password, WIFI_CHANNEL);
}

void wifi_init_sta(const char *sta_ssid, const char *sta_password) {
    esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = {
        .sta = {
            // .ssid = EXAMPLE_ESP_WIFI_STA_SSID,
            // .password = EXAMPLE_ESP_WIFI_STA_PASSWD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = 0,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    strncpy((char *)wifi_sta_config.sta.ssid, sta_ssid, sizeof(wifi_sta_config.sta.ssid));
    wifi_sta_config.sta.ssid[sizeof(wifi_sta_config.sta.ssid)-1] = '\0';

    strncpy((char *)wifi_sta_config.sta.password, sta_password, sizeof(wifi_sta_config.sta.password));
    wifi_sta_config.sta.password[sizeof(wifi_sta_config.sta.password)-1] = '\0';

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");
}

void softap_set_dns_addr(esp_netif_t *esp_netif_ap, esp_netif_t *esp_netif_sta) {
    esp_netif_dns_info_t dns;
    esp_netif_get_dns_info(esp_netif_sta,ESP_NETIF_DNS_MAIN,&dns);
    uint8_t dhcps_offer_option = 0x02; //OFFER_DNS;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(esp_netif_ap));
}

void wifi_init(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL,
                    NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL,
                    NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));


}

void wifi_start(void) {
    if (esp_netif_ap != NULL || esp_netif_sta != NULL) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    } else if (esp_netif_ap != NULL) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    } else if (esp_netif_sta != NULL) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
}
