#include "net.h"

#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

static const char *TAG = "net";
static EventGroupHandle_t events;
#define CONNECTED BIT0
static int started, stopped, sntp_started;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(events, CONNECTED);
    esp_wifi_connect(); // keep trying
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(events, CONNECTED);
  }
}

int net_connect(int timeout_ms) {
  if (!started) {
    events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, CONFIG_FF_WIFI_SSID, sizeof cfg.sta.ssid);
    strncpy((char *)cfg.sta.password, CONFIG_FF_WIFI_PASSWORD, sizeof cfg.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    started = 1;
  } else if (stopped) {
    ESP_ERROR_CHECK(esp_wifi_start());
    stopped = 0;
  }
  EventBits_t bits = xEventGroupWaitBits(events, CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
  if (!(bits & CONNECTED)) {
    ESP_LOGW(TAG, "couldn't join \"%s\"", CONFIG_FF_WIFI_SSID);
    return -1;
  }
  return 0;
}

int net_sync_time(int timeout_ms) {
  if (!sntp_started) {
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) != ESP_OK) return -1;
    sntp_started = 1;
  }
  if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
    ESP_LOGW(TAG, "no time from NTP yet");
    return -1;
  }
  return 0;
}

int net_get(const char *url, char *buf, size_t size) {
  esp_http_client_config_t cfg = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 15000,
  };
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return -1;
  int status = -1;
  if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0) {
    size_t n = 0;
    int got;
    while (n + 1 < size && (got = esp_http_client_read(client, buf + n, size - 1 - n)) > 0) n += got;
    buf[n] = 0;
    status = esp_http_client_get_status_code(client);
  } else {
    ESP_LOGW(TAG, "request failed: %s", url);
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return status;
}

void net_stop(void) {
  if (sntp_started) esp_netif_sntp_deinit();
  if (started && !stopped) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    stopped = 1;
  }
  sntp_started = 0;
}
