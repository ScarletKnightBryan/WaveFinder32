#include <cerrno>
#include <cstring>
#include <unistd.h>

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "login.hpp"

class UDPClient {
private:
    int port;
    const char* ipAddress;
    int udpSocket = -1;
    struct sockaddr_in dest_addr = {};

public:
    UDPClient(const char* ip, int p)
        : port(p), ipAddress(ip)
    {
    }

    UDPClient(const UDPClient&) = delete;

    UDPClient& operator=(const UDPClient&) = delete;

    ~UDPClient()
    {
        if (udpSocket >= 0) {
            close(udpSocket);
            udpSocket = -1;
        }
    }

    void initializeUDP()
    {
        if (udpSocket >= 0) {
            ESP_LOGE("UDP", "Duplicate Socket");
            return;
        }

        udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (udpSocket == -1) {
            ESP_LOGE("UDP", "Failed to create socket");
            return;
        }

        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ipAddress, &dest_addr.sin_addr) != 1) {
            ESP_LOGE("SOCKET", "Invalid server IP: %s", ipAddress);
            close(udpSocket);
            udpSocket = -1;
            return;
        }
    }

    bool sendMessage(const char* message) {
        // Check whether the socket was initialized
        if (udpSocket < 0) {
            ESP_LOGE("UDP", "Socket is not initialized");
            return false;
        }

        // Check for a null message pointer
        if (message == nullptr) {
            ESP_LOGE("UDP", "Message pointer is null");
            return false;
        }

        const int sent = sendto(
            udpSocket,
            message,
            std::strlen(message),
            0,
            reinterpret_cast<const sockaddr*>(&dest_addr),
            sizeof(dest_addr));

        if (sent < 0) {
            ESP_LOGE("UDP", "sendto failed: errno %d", errno);
            return false;
        }

        ESP_LOGI("UDP", "Sent %d bytes", sent);
        return true;
    }
};

