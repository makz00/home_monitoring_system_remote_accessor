/*
 * Home monitoring system
 * Author: Maksymilian Komarnicki
 */

#include <unistd.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

#include "udps_handler.h"
#include "wifi_handler.h"
#include "web_handler.h"

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(wifi_init_sta());

    ESP_ERROR_CHECK(udps_init());

    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    server = start_webserver();

    while (server) {
        sleep(5);
    }
}
