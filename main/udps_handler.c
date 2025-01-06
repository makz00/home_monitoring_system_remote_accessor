/*
 * Home monitoring system
 * Author: Maksymilian Komarnicki
 */

#include "esp_log.h"
#include "esp_err.h"

#include "mdns.h"

#include "udps_handler.h"
#include "espfsp_client_play.h"

#define CONFIG_STREAMER_STACK_SIZE 4096
#define CONFIG_STREAMER_PRIORITY 5

#define CONFIG_STREAMER_CLIENT_LOCAL_PORT_CONTROL 5003
#define CONFIG_STREAMER_CLIENT_LOCAL_PORT_DATA 5004
#define CONFIG_STREAMER_CLIENT_REMOTE_PORT_DATA_CONTROL 5003
#define CONFIG_STREAMER_CLIENT_REMOTE_PORT_DATA_DATA 5004

#define CONFIG_STREAMER_CAMERA_PIXFORMAT ESPFSP_PIXFORMAT_JPEG
#define CONFIG_STREAMER_CAMERA_FRAMESIZE ESPFSP_FRAMESIZE_CIF
#define CONFIG_STREAMER_FRAME_MAX_LENGTH (100 * 1014)
#define CONFIG_STREAMER_FPS 15

#define CONFIG_STREAMER_CAMERA_GRAB_MODE ESPFSP_GRAB_WHEN_EMPTY
#define CONFIG_STREAMER_CAMERA_JPEG_QUALITY 6
#define CONFIG_STREAMER_CAMERA_FB_COUNT 2

#define CONFIG_STREAMER_BUFFERED_FRAMES 10

#define CONFIG_STREAMER_IS_SERVER_LOCAL 1
#define CONFIG_STREAMER_MDNS_SERVER_NAME "espfsp_server"
#define CONFIG_STREAMER_CENTRAL_HOST_IP "192.168.8.142"

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


esp_err_t udps_init(){
    struct esp_ip4_addr server_addr;
    if (CONFIG_STREAMER_IS_SERVER_LOCAL)
    {
        esp_err_t ret = resolve_mdns_host(CONFIG_STREAMER_MDNS_SERVER_NAME, &server_addr);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }
    else
    {
        server_addr.addr = esp_ip4addr_aton(CONFIG_STREAMER_CENTRAL_HOST_IP);
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
            .control_port = CONFIG_STREAMER_CLIENT_LOCAL_PORT_CONTROL,
            .data_port = CONFIG_STREAMER_CLIENT_LOCAL_PORT_DATA,
        },
        .remote = {
            .control_port = CONFIG_STREAMER_CLIENT_REMOTE_PORT_DATA_CONTROL,
            .data_port = CONFIG_STREAMER_CLIENT_REMOTE_PORT_DATA_DATA,
        },
        .data_transport = ESPFSP_TRANSPORT_TCP,
        .remote_addr.addr = server_addr.addr,
        .frame_config = {
            .pixel_format = CONFIG_STREAMER_CAMERA_PIXFORMAT,
            .frame_size = CONFIG_STREAMER_CAMERA_FRAMESIZE,
            .frame_max_len = CONFIG_STREAMER_FRAME_MAX_LENGTH,
            .fps = CONFIG_STREAMER_FPS,
        },
        .cam_config = {
            .cam_grab_mode = CONFIG_STREAMER_CAMERA_GRAB_MODE,
            .cam_jpeg_quality = CONFIG_STREAMER_CAMERA_JPEG_QUALITY,
            .cam_fb_count = CONFIG_STREAMER_CAMERA_FB_COUNT,
        },
        .buffered_fbs = CONFIG_STREAMER_BUFFERED_FRAMES,
    };

    client_handler = espfsp_client_play_init(&streamer_config);
    if (client_handler == NULL) {
        ESP_LOGE(TAG, "Client play ESPFSP init failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}
