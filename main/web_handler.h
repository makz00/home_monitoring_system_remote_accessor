/*
 * Home monitoring system
 * Author: Maksymilian Komarnicki
 */

#pragma once

#include <unistd.h>

typedef void* httpd_handle_t;
typedef int esp_err_t;
typedef const char*  esp_event_base_t;

httpd_handle_t start_webserver(void);

esp_err_t stop_webserver(httpd_handle_t server);

void disconnect_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

void connect_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
