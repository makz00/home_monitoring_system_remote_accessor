/*
 * Home monitoring system
 * Author: Maksymilian Komarnicki
 */

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_event.h"

#include "wifi_handler.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY 10

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "WIFI_HANDLER";

char stored_ssid[WIFI_SSID_MAX_LEN] = {0};
char stored_pass[WIFI_PASS_MAX_LEN] = {0};

static EventGroupHandle_t s_wifi_event_group;
static httpd_handle_t server = NULL;

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "Connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(const char *ssid, const char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    strncpy((char *)sta_config.sta.ssid, ssid, WIFI_SSID_MAX_LEN);
    strncpy((char *)sta_config.sta.password, password, WIFI_PASS_MAX_LEN);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "WiFi STA initiation finished");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to ap SSID:%s password:%s", ssid, password);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ssid, password);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return ESP_FAIL;
    }
}

static void save_wifi_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open("wifi_config", NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "password", password));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
    ESP_LOGI(TAG, "WiFi credentials saved.");
}

static void load_wifi_credentials(char *ssid, char *password) {
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open("wifi_config", NVS_READWRITE, &nvs));

    size_t ssid_len = WIFI_SSID_MAX_LEN;
    size_t pass_len = WIFI_PASS_MAX_LEN;

    nvs_get_str(nvs, "ssid", ssid, &ssid_len);
    nvs_get_str(nvs, "password", password, &pass_len);

    nvs_close(nvs);
}

esp_err_t handle_root_get(httpd_req_t *req) {
    const char *html_form =
        "<!DOCTYPE html>"
        "<html>"
        "<head><title>WiFi Setup</title></head>"
        "<body>"
        "<h1>ESP32 WiFi Configuration</h1>"
        "<form method=\"POST\" action=\"/connect\">"
        "SSID: <input type=\"text\" name=\"ssid\"><br>"
        "Password: <input type=\"password\" name=\"password\"><br>"
        "<input type=\"submit\" value=\"Connect\">"
        "</form>"
        "</body>"
        "</html>";

    httpd_resp_send(req, html_form, strlen(html_form));
    return ESP_OK;
}

esp_err_t handle_connect_post(httpd_req_t *req) {
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ESP_FAIL;
    }
    buf[len] = '\0';

    char ssid[WIFI_SSID_MAX_LEN] = {0};
    char password[WIFI_PASS_MAX_LEN] = {0};
    sscanf(buf, "ssid=%31[^&]&password=%63s", ssid, password);

    ESP_LOGI(TAG, "Received SSID: %s, Password: %s", ssid, password);

    save_wifi_credentials(ssid, password);

    const char* resp = "<h1>Credentials have been set. Now you have to restart module.</h1>";

    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = handle_root_get,
            .user_ctx = NULL
        };
        httpd_uri_t connect_post = {
            .uri = "/connect",
            .method = HTTP_POST,
            .handler = handle_connect_post,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(server, &root_get);
        httpd_register_uri_handler(server, &connect_post);
    }
    return server;
}

static void start_access_point() {
    ESP_LOGI(TAG, "Starting Access Point...");

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Config",
            .password = "",
            .ssid_len = 0,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Access Point started. Connect to WiFi: ESP32_Config");

    start_webserver();
}

esp_err_t wifi_init(void)
{
    bool wifi_connected = false;

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    load_wifi_credentials(stored_ssid, stored_pass);

    if (strlen(stored_ssid) > 0)
    {
        esp_err_t ret  = wifi_init_sta(stored_ssid, stored_pass);
        if (ret != ESP_OK)
        {
            start_access_point();
        }
        else
        {
            wifi_connected = true;
        }
    }
    else
    {
        start_access_point();
    }

    if (!wifi_connected)
    {
        // Access point is started, so WiFi credentials have to be set.
        // The modul has to be restarted in order to read newly set credentials.
        while(1) { vTaskDelay(5000/ portTICK_PERIOD_MS); }
    }

    return ESP_OK;
}
