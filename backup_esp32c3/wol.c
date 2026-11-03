#include "wol.h"
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "wol";

esp_err_t wol_send_magic_packet(const char *mac_str)
{
    if (!mac_str || strlen(mac_str) < 17) {
        ESP_LOGE(TAG, "Invalid MAC address: %s", mac_str ? mac_str : "NULL");
        return ESP_ERR_INVALID_ARG;
    }

    unsigned int m[6];
    // Support either ':' or '-' delimiters
    int parsed = sscanf(mac_str, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
    if (parsed != 6) {
        parsed = sscanf(mac_str, "%x-%x-%x-%x-%x-%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
    }

    if (parsed != 6) {
        ESP_LOGE(TAG, "Failed to parse MAC address string: %s", mac_str);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)m[i];
    }

    // Build 102-byte Magic Packet: 6 bytes 0xFF followed by 16 repetitions of MAC
    uint8_t magic_packet[102];
    memset(magic_packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(&magic_packet[6 + i * 6], mac, 6);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: errno %d", errno);
        return ESP_FAIL;
    }

    int broadcast_enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(9),
        .sin_addr.s_addr = INADDR_BROADCAST,
    };

    ESP_LOGI(TAG, "Sending WOL Magic Packet to %02X:%02X:%02X:%02X:%02X:%02X...",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Send 3 times with 50ms delay for maximum reliability
    for (int attempt = 0; attempt < 3; attempt++) {
        int err = sendto(sock, magic_packet, sizeof(magic_packet), 0,
                         (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "WOL sendto failed (attempt %d): errno %d", attempt + 1, errno);
        } else {
            ESP_LOGI(TAG, "WOL packet #%d sent successfully", attempt + 1);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    close(sock);
    return ESP_OK;
}
