/*
 * Home monitoring system
 * Author: Maksymilian Komarnicki
 */

#include "string.h"

#include "esp_log.h"
#include "esp_err.h"

#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "espfsp_client_play.h"
#include "udps_handler.h"

#define CONFIG_STREAMER_STACK_SIZE 4096
#define CONFIG_STREAMER_PRIORITY 5

#define CONFIG_STREAMER_PORT_CONTROL 5003
#define CONFIG_STREAMER_PORT_DATA 5004

#define CONFIG_STREAMER_FRAME_MAX_LENGTH (100 * 1014)
#define CONFIG_STREAMER_FPS 15
#define CONFIG_STREAMER_BUFFERED_FRAMES 10
#define CONFIG_STREAMER_FRAMSE_BEFORE_GET 0

#define CONFIG_STREAMER_MDNS_SERVER_NAME "espfsp_server"

static const char *TAG = "STREAMER_HANDLER";

espfsp_client_play_handler_t client_handler = NULL;

static esp_err_t resolve_mdns_host(const char * host_name, struct esp_ip4_addr *addr)
{
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGI(TAG, "MDNS Init failed: %d\n", err);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Query A: %s.local", host_name);

    addr->addr = 0;

    err = mdns_query_a(host_name, 2000,  addr);
    if(err){
        if(err == ESP_ERR_NOT_FOUND){
            ESP_LOGI(TAG, "Host was not found!");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Query Failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, IPSTR, IP2STR(addr));
    return ESP_OK;
}


esp_err_t udps_init(const char *server_ip_addr){
    struct esp_ip4_addr server_addr;
    if (strlen(server_ip_addr) == 0)
    {
        while (resolve_mdns_host(CONFIG_STREAMER_MDNS_SERVER_NAME, &server_addr) != ESP_OK)
        {
            vTaskDelay(10000 / portTICK_PERIOD_MS);
        }
    }
    else
    {
        server_addr.addr = esp_ip4addr_aton(server_ip_addr);
    }

    espfsp_client_play_config_t streamer_config = {
        .data_task_info = {
            .stack_size = CONFIG_STREAMER_STACK_SIZE,
            .task_prio = CONFIG_STREAMER_PRIORITY,
        },
        .session_and_control_task_info = {
            .stack_size = CONFIG_STREAMER_STACK_SIZE,
            .task_prio = CONFIG_STREAMER_PRIORITY,
        },
        .local = {
            .control_port = CONFIG_STREAMER_PORT_CONTROL,
            .data_port = CONFIG_STREAMER_PORT_DATA,
        },
        .remote = {
            .control_port = CONFIG_STREAMER_PORT_CONTROL,
            .data_port = CONFIG_STREAMER_PORT_DATA,
        },
        .data_transport = ESPFSP_TRANSPORT_TCP,
        .remote_addr.addr = server_addr.addr,
        .frame_config = {
            .frame_max_len = CONFIG_STREAMER_FRAME_MAX_LENGTH,
            .fps = CONFIG_STREAMER_FPS,
            .buffered_fbs = CONFIG_STREAMER_BUFFERED_FRAMES,
            .fb_in_buffer_before_get = CONFIG_STREAMER_FRAMSE_BEFORE_GET,
        },
    };

    client_handler = espfsp_client_play_init(&streamer_config);
    if (client_handler == NULL) {
        ESP_LOGE(TAG, "Client play ESPFSP init failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t udps_deinit()
{
    espfsp_client_play_deinit(client_handler);
    return ESP_OK;
}
