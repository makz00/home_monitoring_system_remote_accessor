/*
 * Home monitoring system
 * Author: Maksymilian Komarnicki
 */

#pragma once

typedef int esp_err_t;

// server_ip_addr = "" for local server module, then MDNS name should be 'espfsp_server'
// server_ip_addr = <IP> for remote server module;
esp_err_t udps_init(const char *server_ip_addr);
esp_err_t udps_deinit();
