#ifndef ESP8285_WIFI_H
#define ESP8285_WIFI_H

#include <Arduino.h>

class ESP8285Wifi
{
public:
    ESP8285Wifi(SerialUART &serial, int txPin, int rxPin,
                int baudRate = 115200, int fifoSize = 4096);

    bool begin();
    bool connectWiFi(const char *ssid, const char *password);
    void disconnect();
    String getIP();

    String httpGet(const char *host, int port, const char *path);
    String httpPost(const char *host, int port, const char *path,
                    const char *contentType, const char *body);

private:
    SerialUART &_serial;
    int _txPin;
    int _rxPin;
    int _baudRate;
    int _fifoSize;

    String httpRequest(const char *method, const char *host, int port,
                       const char *path, const char *contentType = nullptr,
                       const char *body = nullptr);

    String sendAT(const char *cmd, unsigned long timeout = 3000);
    String waitFor(const char *pattern, unsigned long timeout = 10000);
    int readInto(String &dest, unsigned long timeout,
                 const char *stopPattern = nullptr);
    String stripIPD(const String &response);
    String extractBody(const String &httpData);
};

#endif
