/*
 * Home monitoring system
 * Author: Maksymilian Komarnicki
 */

/*
 * ASSUMPTIONS BEG --------------------------------------------------------------------------------
 * ASSUMPTIONS END --------------------------------------------------------------------------------
 *
 * TODO BEG ---------------------------------------------------------------------------------------
 * - Configuration options to add in 'menuconfig'/Kconfig file
 * TODO END ---------------------------------------------------------------------------------------
 */

#include "esp_log.h"
#include "esp_err.h"

#include "udps_handler.h"
#include "streamer_client.h"

#define CONFIG_STREAMER_STACK_SIZE 4096
#define CONFIG_STREAMER_PRIORITY 5

#define CONFIG_STREAMER_CLIENT_LOCAL_PORT_CONTROL 5003
#define CONFIG_STREAMER_CLIENT_LOCAL_PORT_DATA 5004
#define CONFIG_STREAMER_CLIENT_REMOTE_PORT_DATA_CONTROL 5003
#define CONFIG_STREAMER_CLIENT_REMOTE_PORT_DATA_DATA 5004

#define CONFIG_STREAMER_CENTRAL_HOST_IP "192.168.8.142"

// Streamer-protocol settings
#define CONFIG_STREAMER_FPS 15
#define CONFIG_STREAMER_FRAME_MAX_LENGTH (100 * 1014)
#define CONFIG_STREAMER_BUFFERED_FRAMES 10

static const char *TAG = "UDPS_HANDLER";

esp_err_t udps_init(){
    streamer_config_t streamer_config = {
        .data_receive_task_info = {
            .stack_size = CONFIG_STREAMER_STACK_SIZE,
            .task_prio = CONFIG_STREAMER_PRIORITY,
        },
        .central_control_task_info = {
            .stack_size = CONFIG_STREAMER_STACK_SIZE,
            .task_prio = CONFIG_STREAMER_PRIORITY,
        },
        .client_local_ports = {
            .control_port = CONFIG_STREAMER_CLIENT_LOCAL_PORT_CONTROL,
            .data_port = CONFIG_STREAMER_CLIENT_LOCAL_PORT_DATA,
        },
        .client_remote_ports = {
            .control_port = CONFIG_STREAMER_CLIENT_REMOTE_PORT_DATA_CONTROL,
            .data_port = CONFIG_STREAMER_CLIENT_REMOTE_PORT_DATA_DATA,
        },
        .trans = STREAMER_TRANSPORT_UDP,
        .buffered_fbs = CONFIG_STREAMER_BUFFERED_FRAMES,
        .frame_max_len = CONFIG_STREAMER_FRAME_MAX_LENGTH,
        .fps = CONFIG_STREAMER_FPS,
        .host_ip = CONFIG_STREAMER_CENTRAL_HOST_IP,
    };

    esp_err_t err = streamer_client_init(&streamer_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UDP streamer handler central Init Failed");
        return err;
    }

    return ESP_OK;
}
