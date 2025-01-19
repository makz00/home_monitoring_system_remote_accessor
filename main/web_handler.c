/*
 * Home monitoring system
 * Author: Maksymilian Komarnicki
 */

#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"
#include "esp_http_server.h"

#include "index_html_gz.h"
#include "espfsp_client_play.h"

static const char *TAG = "WEB_HANDLER";

extern espfsp_client_play_handler_t client_handler;

esp_err_t start_stream_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Start stream request received");
    httpd_resp_send(req, "Stream started", HTTPD_RESP_USE_STRLEN);
    return espfsp_client_play_start_stream(client_handler);
}

esp_err_t stop_stream_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Stop stream request received");
    httpd_resp_send(req, "Stream stopped", HTTPD_RESP_USE_STRLEN);
    return espfsp_client_play_stop_stream(client_handler);
}

esp_err_t stream_handler(httpd_req_t *req) {
    char part_buf[64];
    int failed_fb_threshold = 10;
    int failed_fb = 0;

    // Ustawienie nagłówków odpowiedzi
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        espfsp_fb_t *fb = espfsp_client_play_get_fb(client_handler, 400);
        if (!fb) {
            ESP_LOGE(TAG, "Błąd pobierania ramki");
            failed_fb++;
            if (failed_fb > failed_fb_threshold)
            {
                httpd_resp_send(req, NULL, 0);
                return ESP_OK;
            }
            continue;
        }

        failed_fb = 0;

        // Wygeneruj nagłówki dla ramki
        size_t hlen = snprintf(part_buf, sizeof(part_buf),
                               "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                               fb->len);
        // Wyślij nagłówki
        if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
            espfsp_client_play_return_fb(client_handler, fb);
            ESP_LOGE(TAG, "Błąd wysyłania nagłówków ramki");
            break;
        }

        // Wyślij ramkę obrazu
        if (httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) {
            espfsp_client_play_return_fb(client_handler, fb);
            ESP_LOGE(TAG, "Błąd wysyłania ramki");
            break;
        }

        espfsp_client_play_return_fb(client_handler, fb);

        // Wyślij zakończenie ramki
        if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
            ESP_LOGE(TAG, "Błąd zakończenia ramki");
            break;
        }

        vTaskDelay(30 / portTICK_PERIOD_MS);
    }

    // Zakończ połączenie
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_send(req, (const char *)index_html_gz, index_html_gz_len);
    return ESP_OK;
}

esp_err_t get_src_handler(httpd_req_t *req) {
    char sources_names[5][30];
    int sources_count = 5;

    esp_err_t ret = espfsp_client_play_get_sources_timeout(client_handler, sources_names, &sources_count, 1000);
    if (ret == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Get src failed");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Received sources count: %d", sources_count);

    char json_response[512];
    char *ptr = json_response;

    // Rozpocznij tablicę JSON
    ptr += sprintf(ptr, "[");

    // Dodaj każdy element jako ciąg JSON
    for (size_t i = 0; i < sources_count; ++i) {
        ptr += sprintf(ptr, "\"%s\"", sources_names[i]);
        if (i < sources_count - 1) {
            ptr += sprintf(ptr, ","); // Dodaj przecinek między elementami
        }
    }

    // Zakończ tablicę JSON
    sprintf(ptr, "]");

    // Ustawienie nagłówków odpowiedzi
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Wysłanie odpowiedzi
    return httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t set_src_handler(httpd_req_t *req) {
    char query[128];
    char name[30];

    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len > 1) {
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            ESP_LOGI("QUERY", "Query string: %s", query);

            if (httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK) {
                ESP_LOGI("QUERY", "Value of 'name': %s", name);
            }
        }
    }

    esp_err_t ret = espfsp_client_play_set_source(client_handler, name);
    if (ret == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Set src failed");
    }

    ESP_LOGI(TAG, "Src set");

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t set_frame_handler(httpd_req_t *req) {
    espfsp_frame_config_t frame_config = {
        .fps = 15,
        .frame_max_len = (100 * 1014),
        .buffered_fbs = 10,
        .fb_in_buffer_before_get = 0,
    };

    char query[128];
    char fps[30];
    char frame_max_len[30];
    char buffered_fbs[30];
    char fb_in_buffer_before_get[30];

    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len > 1) {
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            ESP_LOGI("QUERY", "Query string: %s", query);

            if (httpd_query_key_value(query, "fps", fps, sizeof(fps)) == ESP_OK) {
                ESP_LOGI("QUERY", "Value of 'fps': %s", fps);
                frame_config.fps = atoi(fps);
                if (frame_config.fps == 0)
                {
                    ESP_LOGE(TAG, "FSP conv failed");
                    frame_config.fps = 1;
                }
            }

            if (httpd_query_key_value(query, "frame_max_len", frame_max_len, sizeof(frame_max_len)) == ESP_OK) {
                ESP_LOGI("QUERY", "Value of 'frame_max_len': %s", frame_max_len);
                frame_config.frame_max_len = atoi(frame_max_len);
                if (frame_config.frame_max_len == 0)
                {
                    ESP_LOGE(TAG, "frame_max_len conv failed");
                    frame_config.frame_max_len = 1;
                }
            }

            if (httpd_query_key_value(query, "buffered_fbs", buffered_fbs, sizeof(buffered_fbs)) == ESP_OK) {
                ESP_LOGI("QUERY", "Value of 'buffered_fbs': %s", buffered_fbs);
                frame_config.buffered_fbs = atoi(buffered_fbs);
                if (frame_config.buffered_fbs == 0)
                {
                    ESP_LOGE(TAG, "buffered_fbs conv failed");
                    frame_config.buffered_fbs = 1;
                }
            }

            if (httpd_query_key_value(query, "fb_in_buffer_before_get", fb_in_buffer_before_get, sizeof(fb_in_buffer_before_get)) == ESP_OK) {
                ESP_LOGI("QUERY", "Value of 'fb_in_buffer_before_get': %s", fb_in_buffer_before_get);
                frame_config.fb_in_buffer_before_get = atoi(fb_in_buffer_before_get);
            }
        }
    }

    esp_err_t ret = espfsp_client_play_reconfigure_frame(client_handler, &frame_config);
    if (ret == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Send fram config failed");
    }

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t set_cam_handler(httpd_req_t *req) {
    espfsp_cam_config_t cam_config = {
        .cam_fb_count = 2, // Useless now
        .cam_grab_mode = ESPFSP_GRAB_LATEST, // Useless now
        .cam_jpeg_quality = 30,
        .cam_frame_size = ESPFSP_FRAMESIZE_96X96,
        .cam_pixel_format = ESPFSP_PIXFORMAT_JPEG,
    };

    char query[128];
    char cam_jpeg_quality[30];
    char cam_frame_size[30];
    char cam_pixel_format[30];

    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len > 1) {
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            ESP_LOGI("QUERY", "Query string: %s", query);

            if (httpd_query_key_value(query, "cam_jpeg_quality", cam_jpeg_quality, sizeof(cam_jpeg_quality)) == ESP_OK) {
                ESP_LOGI("QUERY", "Value of 'cam_jpeg_quality': %s", cam_jpeg_quality);
                cam_config.cam_jpeg_quality = atoi(cam_jpeg_quality);
                if (cam_config.cam_jpeg_quality == 0)
                {
                    ESP_LOGE(TAG, "FSP conv failed");
                    cam_config.cam_jpeg_quality = 1;
                }
            }

            if (httpd_query_key_value(query, "cam_frame_size", cam_frame_size, sizeof(cam_frame_size)) == ESP_OK) {
                ESP_LOGI("QUERY", "Value of 'pixel_format': %s", cam_frame_size);
                cam_config.cam_frame_size = atoi(cam_frame_size);
            }

            if (httpd_query_key_value(query, "cam_pixel_format", cam_pixel_format, sizeof(cam_pixel_format)) == ESP_OK) {
                ESP_LOGI("QUERY", "Value of 'cam_pixel_format': %s", cam_pixel_format);
                cam_config.cam_pixel_format = atoi(cam_pixel_format);
            }
        }
    }

    esp_err_t ret = espfsp_client_play_reconfigure_cam(client_handler, &cam_config);
    if (ret == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Send cam config failed");
    }

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

httpd_uri_t index_uri = {
    .uri = "/index",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

httpd_uri_t start_stream_uri = {
    .uri = "/start_stream",
    .method = HTTP_GET,
    .handler = start_stream_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

httpd_uri_t stop_stream_uri = {
    .uri = "/stop_stream",
    .method = HTTP_GET,
    .handler = stop_stream_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

httpd_uri_t get_src_uri = {
    .uri = "/get_sources",
    .method = HTTP_GET,
    .handler = get_src_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

httpd_uri_t set_src_uri = {
    .uri = "/set_source",
    .method = HTTP_GET,
    .handler = set_src_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

httpd_uri_t set_frame_uri = {
    .uri = "/set_frame",
    .method = HTTP_GET,
    .handler = set_frame_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

httpd_uri_t set_cam_uri = {
    .uri = "/set_cam",
    .method = HTTP_GET,
    .handler = set_cam_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Ustawienie Keep-Alive timeout
    config.lru_purge_enable = true; // LRU Cache dla zarządzania połączeniami
    // config.keep_alive_enable = true; // Włączenie Keep-Alive
    // config.keep_alive_idle = 10; // Czas (w sekundach), przez jaki połączenie pozostaje otwarte bez aktywności
    // config.keep_alive_interval = 5; // Interwał między Keep-Alive a żądaniem
    // config.keep_alive_count = 3; // Maksymalna liczba Keep-Alive prób

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &start_stream_uri);
        httpd_register_uri_handler(server, &stop_stream_uri);
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &get_src_uri);
        httpd_register_uri_handler(server, &set_src_uri);
        httpd_register_uri_handler(server, &set_frame_uri);
        httpd_register_uri_handler(server, &set_cam_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

httpd_handle_t start_stream_server(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Ustawienie Keep-Alive timeout
    config.lru_purge_enable = true; // LRU Cache dla zarządzania połączeniami
    // config.keep_alive_enable = true; // Włączenie Keep-Alive
    // config.keep_alive_idle = 10; // Czas (w sekundach), przez jaki połączenie pozostaje otwarte bez aktywności
    // config.keep_alive_interval = 5; // Interwał między Keep-Alive a żądaniem
    // config.keep_alive_count = 3; // Maksymalna liczba Keep-Alive prób

    // ra_filter_init(&ra_filter, 20);

    config.server_port += 1;
    config.ctrl_port += 1;

    ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &stream_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

esp_err_t stop_server(httpd_handle_t server)
{
    return httpd_stop(server);
}

void connect_server(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

void disconnect_server(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_server(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

void connect_stream_server(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting stream server");
        *server = start_stream_server();
    }
}

void disconnect_stream_server(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping stream server");
        if (stop_server(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}
