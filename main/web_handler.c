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

// #include "index_html_gz.h"
#include "camera_stream_page_v3.h"
#include "espfsp_client_play.h"

static const char *TAG = "WEB_HANDLER";

extern espfsp_client_play_handler_t client_handler;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

typedef struct
{
    size_t size;    //number of values used for filtering
    size_t index;   //current value index
    size_t count;   //value count
    int sum;
    int *values;    //array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values)
    {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t *filter, int value)
{
    if (!filter->values)
    {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size)
    {
        filter->count++;
    }
    return filter->sum / filter->count;
}

esp_err_t stream_handler(httpd_req_t *req)
{
    espfsp_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    char *_jpg_buf = NULL;
    char *part_buf[128];

    static int64_t last_frame = 0;
    if (!last_frame)
    {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    while (true)
    {
        fb = espfsp_client_play_get_fb(client_handler);

        if (!fb)
        {
            ESP_LOGE(TAG, "Frame capture failed");
            res = ESP_FAIL;
        }
        else
        {
            _timestamp.tv_sec = fb->timestamp.tv_sec;
            _timestamp.tv_usec = fb->timestamp.tv_usec;

            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (fb)
        {
            res = espfsp_client_play_return_fb(client_handler, fb);
            if (res != ESP_OK)
            {
                ESP_LOGE(TAG, "Return frame failed");
            }
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            ESP_LOGE(TAG, "Send frame failed");
            break;
        }
        int64_t fr_end = esp_timer_get_time();

        int64_t frame_time = fr_end - last_frame;
        frame_time /= 1000;

        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);

        ESP_LOGI(TAG, "MJPG: %" PRIu32 "B %" PRIu32 "ms (%.1ffps), AVG: %" PRIu32 "ms (%.1ffps)",
                 (uint32_t)(_jpg_buf_len),
                 (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
                 avg_frame_time, 1000.0 / avg_frame_time);

        last_frame = fr_end;
    }

    return res;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)camera_stream_page, camera_stream_page_len);
}

static bool stream_started = false;

static esp_err_t start_stream_handler(httpd_req_t *req)
{
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Start stream");

    if (!stream_started)
    {
        err = espfsp_client_play_start_stream(client_handler);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Start stream failed");
    }
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Stream started");
        stream_started = true;
    }

    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t stop_stream_handler(httpd_req_t *req)
{
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Stop stream");

    if (stream_started)
    {
        err = espfsp_client_play_stop_stream(client_handler);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Stop stream failed");
    }
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Stream stopped");
        stream_started = false;
    }

    return httpd_resp_send(req, NULL, 0);
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


httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.lru_purge_enable = true;

    ra_filter_init(&ra_filter, 20);

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &start_stream_uri);
        httpd_register_uri_handler(server, &stop_stream_uri);
        httpd_register_uri_handler(server, &index_uri);

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

esp_err_t stop_webserver(httpd_handle_t server)
{
    return httpd_stop(server);
}

void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}
