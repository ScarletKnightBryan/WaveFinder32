#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <vector>

#include "esp_netif_sntp.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#include "login.hpp"

namespace {

static std::vector<float> previousMagnitudes;
static std::vector<float> baselineSum;
static bool hasPreviousPacket = false;

void initializeSNTP()
{
    esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
}
void csiCallback(void* ctx, wifi_csi_info_t* data)
{
    if (data == nullptr) {
        ESP_LOGE("CSI", "Received null CSI data.");
        return;
    }
    ESP_LOGI("CSI", "Metadata dBm: %d Metadata Channel: %d",
             data->rx_ctrl.rssi, data->rx_ctrl.channel);

    if (data->buf == nullptr || data->len == 0) {
        ESP_LOGW("CSI", "Received empty CSI data.");
        return;
    }
    if (data->len < 2) {
        ESP_LOGW("CSI", "Incomplete I/Q pair.");
        return;
    }
    int8_t* csi_data;
    if (data->first_word_invalid) {
        ESP_LOGW("CSI", "First four CSI bytes are invalid.");
    }

    size_t offsetData = data->first_word_invalid ? 4 : 0;
    if (data->len <= offsetData) {
        return;
    }

    csi_data = &data->buf[0];
    size_t pairCount = (data->len - offsetData) / 2;
    float totalMagnitudeChange = 0;
    static int consecutiveMotionCount = 0;
    static int previousChannel = 0;

    if (previousChannel != data->rx_ctrl.channel) {
        hasPreviousPacket = false;
        consecutiveMotionCount = 0;
    }

    if (pairCount != previousMagnitudes.size()) {
        previousMagnitudes.resize(pairCount);
        hasPreviousPacket = false;
        consecutiveMotionCount = 0;
    }
    previousChannel = data->rx_ctrl.channel;
    bool wasBaselineReady = hasPreviousPacket;

    for (size_t i = offsetData; i + 1 < data->len; i += 2) {
        size_t index = (i - offsetData) / 2;
        int8_t imaginary = csi_data[i];
        int8_t real = csi_data[i + 1];
        int sum = real * real + imaginary * imaginary;
        float magnitude = std::sqrt(sum);
        if (!hasPreviousPacket) {
            previousMagnitudes[index] = magnitude;
            continue;
        }

        float previousMagnitude = previousMagnitudes[index];
        float magnitudeChange = std::abs(magnitude - previousMagnitude);
        totalMagnitudeChange += magnitudeChange;
        previousMagnitudes[index] = magnitude;
        ESP_LOGD("CSI", "Magnitude Change: %f", magnitudeChange);

        ESP_LOGD("CSI", "I: %d, Q: %d", static_cast<int>(real),
                 static_cast<int>(imaginary));
    }
    if (wasBaselineReady) {
        float averageMagnitude = totalMagnitudeChange / pairCount;
        float motionThreshold = 5.0f;
        ESP_LOGI("CSI", "Example value: %f", averageMagnitude);
        if (averageMagnitude > motionThreshold) {
            consecutiveMotionCount++;
            if (consecutiveMotionCount == 3) {
                time_t now;
                time(&now);

                struct tm utcTime;
                gmtime_r(&now, &utcTime);

                char timestamp[32];
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
                         &utcTime);
                ESP_LOGI("CSI", "[%s UTC] Potential motion detected!",
                         timestamp);
            }
        } else {
            consecutiveMotionCount = 0;
        }
    }
    hasPreviousPacket = true;

    ESP_LOGI("CSI", "Packets: %u", static_cast<unsigned int>(data->len));
}

void wifiEventHandler(void*, esp_event_base_t event_base, int32_t event_id,
                      void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI("WIFI", "WiFi connected successfully.");
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_err_t result = esp_wifi_connect();
        ESP_LOGI("WIFI", "WiFi disconnected.");

        if (result == ESP_OK) {
            ESP_LOGI("WIFI", "Connection attempt started.");
        } else {
            ESP_LOGE("WIFI", "Connection attempt failed: %s",
                     esp_err_to_name(result));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI("WIFI", "ESP32 obtained an IP address!");
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI("WIFI", "IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void initializeCSI()
{
    wifi_csi_config_t csi_config = wifi_csi_config_t();
    csi_config.lltf_en = true;
    csi_config.htltf_en = true;
    csi_config.stbc_htltf2_en = true;
    csi_config.ltf_merge_en = true;
    csi_config.channel_filter_en = true;
    csi_config.manu_scale = false;
    csi_config.shift = 0;
    csi_config.dump_ack_en = false;

    const wifi_csi_config_t* config(&csi_config);
    esp_err_t result = esp_wifi_set_csi_config(config);
    if (result == ESP_OK) {
        ESP_LOGI("CSI", "CSI configuration set successfully.");
        esp_wifi_set_csi_rx_cb(csiCallback, nullptr);
        esp_wifi_set_csi(true);
    } else {
        ESP_LOGE("CSI", "Failed to set CSI configuration: %s",
                 esp_err_to_name(result));
    }
}

void startWifiProcess()
{
    ESP_LOGI("WIFI", "Starting WiFi process...");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEventHandler, nullptr));

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_netif_t* netif = esp_netif_create_default_wifi_sta();

    assert(netif != nullptr);

    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {};
    std::strcpy(reinterpret_cast<char*>(wifi_config.sta.ssid), WIFI_SSID);
    std::strcpy(reinterpret_cast<char*>(wifi_config.sta.password),
                WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    initializeCSI();
    initializeSNTP();
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI("WIFI", "WiFi process completed.");
}
}  // namespace

extern "C" void app_main(void)
{
    startWifiProcess();
}
