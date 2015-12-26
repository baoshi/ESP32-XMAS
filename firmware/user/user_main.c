#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"

#include "esp_common.h"

#include "lcd.h"

#include "websocket.h"


xSemaphoreHandle wifi_alive;

#define STA_SSID    "XXXX"
#define STA_PASSWORD  "SECRET"


static void wifi_task(void *pvParameters)
{
    uint8_t status;

    if (wifi_get_opmode() != STATION_MODE)
    {
        wifi_set_opmode(STATION_MODE);
        vTaskDelay(1000 / portTICK_RATE_MS);
        system_restart();
    }

    while (1)
    {
        printf("WiFi: Connecting to WiFi\n");
        wifi_station_connect();
        struct station_config config;
        bzero(&config, sizeof(struct station_config));
        sprintf(config.ssid, STA_SSID);
        sprintf(config.password, STA_PASSWORD);
        wifi_station_set_config(&config);
        vTaskDelay(1000 / portTICK_RATE_MS);
        status = wifi_station_get_connect_status();
        int8_t retries = 30;
        while ((status != STATION_GOT_IP) && (retries > 0))
        {
            status = wifi_station_get_connect_status();
            if (status == STATION_WRONG_PASSWORD)
            {
                printf("WiFi: Wrong password\n");
                break;
            }
            else if (status == STATION_NO_AP_FOUND)
            {
                printf("WiFi: AP not found\n");
                break;
            }
            else if (status == STATION_CONNECT_FAIL)
            {
                printf("WiFi: Connection failed\n");
                break;
            }
            vTaskDelay(1000 / portTICK_RATE_MS);
            --retries;
        }
        if (status == STATION_GOT_IP)
        {
            printf("WiFi: Connected\n");
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
        while ((status = wifi_station_get_connect_status()) == STATION_GOT_IP)
        {
            xSemaphoreGive(wifi_alive);
            // dmsg_puts("WiFi: Alive\n");
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
        printf("WiFi: Disconnected\n");
        wifi_station_disconnect();
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}



#define LCD_BUF_LEN (128 * 160 * 2)
#define WS_BUF_LEN (LCD_BUF_LEN + 128)

uint8 *lcdBuffer = (uint8*)0x3ffa8000;
uint8 *wsBuffer = (uint8*)0x3ffb2000;


// #define PACKET_DUMP


int safeSend(int clientSocket, const uint8_t *buffer, size_t bufferSize)
{
#ifdef PACKET_DUMP
    printf("out packet:\n");
    fwrite(buffer, 1, bufferSize, stdout);
    printf("\n");
#endif
    ssize_t written = send(clientSocket, buffer, bufferSize, 0);
    if (written == -1)
    {
        close(clientSocket);
        printf("send failed");
        return EXIT_FAILURE;
    }
    if (written != bufferSize)
    {
        close(clientSocket);
        printf("written not all bytes");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


void clientWorker(int clientSocket)
{
    memset(wsBuffer, 0, WS_BUF_LEN);
    size_t readedLength = 0;
    size_t frameSize = WS_BUF_LEN;
    enum wsState state = WS_STATE_OPENING;
    uint8_t *data = NULL;
    size_t dataSize = 0;
    enum wsFrameType frameType = WS_INCOMPLETE_FRAME;
    struct handshake hs;
    nullHandshake(&hs);

    #define prepareBuffer frameSize = WS_BUF_LEN; memset(wsBuffer, 0, WS_BUF_LEN);
    #define initNewFrame frameType = WS_INCOMPLETE_FRAME; readedLength = 0; memset(wsBuffer, 0, WS_BUF_LEN);

    while (frameType == WS_INCOMPLETE_FRAME)
    {
        ssize_t readed = recv(clientSocket, wsBuffer + readedLength, WS_BUF_LEN - readedLength, 0);
        if (!readed)
        {
            close(clientSocket);
            printf("recv failed\n");
            return;
        }
#ifdef PACKET_DUMP
        printf("in packet:\n");
        fwrite(wsBuffer, 1, readed, stdout);
        printf("\n");
#endif
        readedLength += readed;
        // assert(readedLength <= BUF_LEN);
        if (state == WS_STATE_OPENING)
        {
            frameType = wsParseHandshake(wsBuffer, readedLength, &hs);
        }
        else
        {
            frameType = wsParseInputFrame(wsBuffer, readedLength, &data, &dataSize);
        }

        if ((frameType == WS_INCOMPLETE_FRAME && readedLength == WS_BUF_LEN) || frameType == WS_ERROR_FRAME)
        {
            if (frameType == WS_INCOMPLETE_FRAME)
                printf("buffer too small\n");
            else
                printf("error in incoming frame\n");

            if (state == WS_STATE_OPENING)
            {
                prepareBuffer;
                frameSize = sprintf((char *)wsBuffer,
                                    "HTTP/1.1 400 Bad Request\r\n"
                                    "%s%s\r\n\r\n",
                                    versionField,
                                    version);
                safeSend(clientSocket, wsBuffer, frameSize);
                break;
            }
            else
            {
                prepareBuffer;
                wsMakeFrame(NULL, 0, wsBuffer, &frameSize, WS_CLOSING_FRAME);
                if (safeSend(clientSocket, wsBuffer, frameSize) == EXIT_FAILURE)
                    break;
                state = WS_STATE_CLOSING;
                initNewFrame;
            }
        }

        if (state == WS_STATE_OPENING)
        {
            // assert(frameType == WS_OPENING_FRAME);
            if (frameType == WS_OPENING_FRAME)
            {
                // if resource is right, generate answer handshake and send it
                if (strcmp(hs.resource, "/video") != 0)
                {
                    frameSize = sprintf((char *)wsBuffer, "HTTP/1.1 404 Not Found\r\n\r\n");
                    safeSend(clientSocket, wsBuffer, frameSize);
                    break;
                }

                prepareBuffer;
                wsGetHandshakeAnswer(&hs, wsBuffer, &frameSize);
                freeHandshake(&hs);
                if (safeSend(clientSocket, wsBuffer, frameSize) == EXIT_FAILURE)
                    break;
                state = WS_STATE_NORMAL;
                initNewFrame;
            }
        }
        else
        {
            if (frameType == WS_CLOSING_FRAME)
            {
                if (state == WS_STATE_CLOSING)
                {
                    break;
                }
                else
                {
                    prepareBuffer;
                    wsMakeFrame(NULL, 0, wsBuffer, &frameSize, WS_CLOSING_FRAME);
                    safeSend(clientSocket, wsBuffer, frameSize);
                    break;
                }
            }
            else if (frameType == WS_BINARY_FRAME)
            {
                if (dataSize == LCD_BUF_LEN)
                {
                    printf("FRM\n");
                    memcpy(lcdBuffer, data, LCD_BUF_LEN);
                    lcdWriteFrame();
                }
                if (readedLength > dataSize)
                {
                    // Copy next frame to the beginning and continue
                    readedLength = readedLength - dataSize - (data - wsBuffer);
                    memcpy(wsBuffer, data + dataSize, readedLength);
                    frameType = WS_INCOMPLETE_FRAME;
                }
                else
                {
                    initNewFrame;
                }
            }
            else if (frameType == WS_PING_FRAME)
            {
                if (state != WS_STATE_CLOSING)
                {
                    prepareBuffer;
                    wsMakeFrame(NULL, 0, wsBuffer, &frameSize, WS_PONG_FRAME);
                    if (safeSend(clientSocket, wsBuffer, frameSize) == EXIT_FAILURE)
                        break;
                    initNewFrame;
                }
            }
        }
    } // read/write cycle

    close(clientSocket);
}


void svr_task(void *pvParameters)
{
    while (1)
    {
        struct sockaddr_in local;
        int listenSocket;

        do
        {
            // Wait until wifi is up
            //xSemaphoreTake(wifi_alive, portMAX_DELAY);

            if (-1 == (listenSocket = socket(AF_INET, SOCK_STREAM, 0)))
            {
                printf("S > socket error\n");
                break;
            }
            printf("S > create socket: %d\n", listenSocket);

            bzero(&local, sizeof(struct sockaddr_in));
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = INADDR_ANY;
            local.sin_port = htons(80);
            if (-1 == bind(listenSocket, (struct sockaddr *) (&local), sizeof(local)))
            {
                printf("S > bind fail\n");
                break;
            }
            printf("S > bind port: %d\n", ntohs(local.sin_port));

            if (-1 == listen(listenSocket, 1))
            {
                printf("S > listen fail\n");
                break;
            }
            printf("S > listening\n");

            while (1)
            {
                struct sockaddr_in remote;
                socklen_t sockaddrLen = sizeof(remote);
                int clientSocket;

                printf("S > wait client\n");
                if ((clientSocket = accept(listenSocket, (struct sockaddr *) &remote, &sockaddrLen)) < 0)
                {
                    printf("S > accept fail\n");
                    break;
                }
                printf("S > Client from %s %d\n", inet_ntoa(remote.sin_addr), htons(remote.sin_port));
                clientWorker(clientSocket);
                printf("heap free size: %d\n", system_get_free_heap_size());
            }
        } while (0);
    }
}


/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
    printf("SDK version:%s\n", system_get_sdk_version());
    printf("CPU running at %dMHz\n", system_get_cpu_freq());
    printf("Free Heap size: %d\n", system_get_free_heap_size());
    system_print_meminfo();

    if (wifi_get_opmode() != SOFTAP_MODE)
    {
        wifi_set_opmode(SOFTAP_MODE);
        vTaskDelay(1000 / portTICK_RATE_MS);
        system_restart();
    }

    lcdInit(lcdBuffer);
    lcdWriteFrame();

    vSemaphoreCreateBinary(wifi_alive);
    xSemaphoreTake(wifi_alive, 0);  // take the default semaphore
    xTaskCreate(svr_task, "srv", 1024, NULL, tskIDLE_PRIORITY + 2, NULL);
    //xTaskCreate(wifi_task, "wifi", 256, NULL, tskIDLE_PRIORITY + 1, NULL);
}

