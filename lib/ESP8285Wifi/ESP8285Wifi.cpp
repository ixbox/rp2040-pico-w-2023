#include "ESP8285Wifi.h"

ESP8285Wifi::ESP8285Wifi(SerialUART &serial, int txPin, int rxPin,
                         int baudRate, int fifoSize)
    : _serial(serial), _txPin(txPin), _rxPin(rxPin),
      _baudRate(baudRate), _fifoSize(fifoSize)
{
}

bool ESP8285Wifi::begin()
{
    _serial.setTX(_txPin);
    _serial.setRX(_rxPin);
    _serial.setFIFOSize(_fifoSize);
    _serial.begin(_baudRate);
    delay(1000);
    while (_serial.available())
        _serial.read();

    String r = sendAT("AT");
    return r.indexOf("OK") >= 0;
}

bool ESP8285Wifi::connectWiFi(const char *ssid, const char *password)
{
    sendAT("AT+CWMODE=1");
    sendAT("AT+CIPMUX=0");

    // SSID(32) + password(63) + AT+CWJAP="","" = ~110 bytes
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    String r = sendAT(cmd, 15000);
    return r.indexOf("OK") >= 0 || r.indexOf("CONNECTED") >= 0;
}

void ESP8285Wifi::disconnect()
{
    sendAT("AT+CWQAP");
}

String ESP8285Wifi::getIP()
{
    return sendAT("AT+CIFSR", 3000);
}

// --- Public convenience methods ---

String ESP8285Wifi::httpGet(const char *host, int port, const char *path)
{
    return httpRequest("GET", host, port, path);
}

String ESP8285Wifi::httpPost(const char *host, int port, const char *path,
                             const char *contentType, const char *body)
{
    return httpRequest("POST", host, port, path, contentType, body);
}

// --- Core HTTP implementation ---

String ESP8285Wifi::httpRequest(const char *method, const char *host, int port,
                                const char *path, const char *contentType,
                                const char *body)
{
    // Open TCP connection
    char cipstart[256];
    snprintf(cipstart, sizeof(cipstart),
             "AT+CIPSTART=\"TCP\",\"%s\",%d", host, port);
    String r = sendAT(cipstart, 10000);
    if (r.indexOf("OK") < 0 && r.indexOf("ALREADY") < 0 &&
        r.indexOf("CONNECT") < 0)
    {
        return "";
    }

    // Build HTTP request header
    char header[384];
    int bodyLen = body ? (int)strlen(body) : 0;

    if (body && contentType)
    {
        snprintf(header, sizeof(header),
                 "%s %s HTTP/1.0\r\n"
                 "Host: %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %d\r\n"
                 "Connection: close\r\n\r\n",
                 method, path, host, contentType, bodyLen);
    }
    else
    {
        snprintf(header, sizeof(header),
                 "%s %s HTTP/1.0\r\n"
                 "Host: %s\r\n"
                 "Connection: close\r\n\r\n",
                 method, path, host);
    }

    // Tell the module total send length (header + body)
    int totalLen = (int)strlen(header) + bodyLen;
    char lenCmd[32];
    snprintf(lenCmd, sizeof(lenCmd), "AT+CIPSEND=%d", totalLen);
    r = sendAT(lenCmd, 3000);

    if (r.indexOf(">") < 0 && waitFor(">", 3000).indexOf(">") < 0)
    {
        sendAT("AT+CIPCLOSE");
        return "";
    }

    // Send header, then body separately to avoid a single large buffer
    _serial.print(header);
    if (body && bodyLen > 0)
        _serial.print(body);

    // Wait for connection close
    String response = waitFor("CLOSED", 15000);

    // Strip +IPD framing and extract HTTP body
    String httpData = stripIPD(response);
    return extractBody(httpData);
}

// --- Private helpers ---

String ESP8285Wifi::sendAT(const char *cmd, unsigned long timeout)
{
    while (_serial.available())
        _serial.read();

    _serial.print(cmd);
    _serial.print("\r\n");

    String resp;
    resp.reserve(512);
    readInto(resp, timeout, "OK");
    return resp;
}

String ESP8285Wifi::waitFor(const char *pattern, unsigned long timeout)
{
    String resp;
    resp.reserve(2048);
    readInto(resp, timeout, pattern);
    return resp;
}

int ESP8285Wifi::readInto(String &dest, unsigned long timeout,
                          const char *stopPattern)
{
    int totalRead = 0;
    unsigned long start = millis();
    while (millis() - start < timeout)
    {
        int avail = _serial.available();
        if (avail > 0)
        {
            char buf[128];
            int toRead = min(avail, (int)sizeof(buf));
            for (int i = 0; i < toRead; i++)
                buf[i] = (char)_serial.read();
            dest.concat(buf, toRead);
            totalRead += toRead;

            if (stopPattern &&
                (dest.indexOf(stopPattern) >= 0 ||
                 dest.indexOf("ERROR") >= 0 ||
                 dest.indexOf("FAIL") >= 0))
            {
                break;
            }
        }
        else
        {
            delay(5);
        }
    }
    return totalRead;
}

String ESP8285Wifi::stripIPD(const String &response)
{
    String httpData;
    int pos = 0;
    while (pos < (int)response.length())
    {
        int ipdPos = response.indexOf("+IPD,", pos);
        if (ipdPos < 0)
            break;
        int colonPos = response.indexOf(':', ipdPos);
        if (colonPos < 0)
            break;
        String lenStr = response.substring(ipdPos + 5, colonPos);
        int dataLen = lenStr.toInt();
        int dataStart = colonPos + 1;
        if (dataLen > 0 && dataStart + dataLen <= (int)response.length())
        {
            httpData += response.substring(dataStart, dataStart + dataLen);
            pos = dataStart + dataLen;
        }
        else
        {
            httpData += response.substring(dataStart);
            break;
        }
    }
    return httpData;
}

String ESP8285Wifi::extractBody(const String &httpData)
{
    int bodyStart = httpData.indexOf("\r\n\r\n");
    if (bodyStart < 0)
        return "";

    return httpData.substring(bodyStart + 4);
}
