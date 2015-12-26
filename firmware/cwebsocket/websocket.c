/*
 * Copyright (c) 2014 Putilov Andrey
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdint.h>
#include <ctype.h>

#include "lwip/sockets.h"
#include "base64.h"
#include "aw-sha1.h"
#include "websocket.h"
#include "esp_common.h"

static char rn[] = "\r\n";

void nullHandshake(struct handshake *hs)
{
    hs->host = NULL;
    hs->origin = NULL;
    hs->resource = NULL;
    hs->key = NULL;
    hs->frameType = WS_EMPTY_FRAME;
}

void freeHandshake(struct handshake *hs)
{
    if (hs->host)
    {
        free(hs->host);
    }
    if (hs->origin)
    {
        free(hs->origin);
    }
    if (hs->resource)
    {
        free(hs->resource);
    }
    if (hs->key)
    {
        free(hs->key);
    }
    nullHandshake(hs);
}

static char* getUptoLinefeed(const char *startFrom)
{
    char *writeTo = NULL;
    uint8_t newLength = strstr(startFrom, rn) - startFrom;
    // assert(newLength);
    writeTo = (char *)malloc(newLength+1); //+1 for '\x00'
    // assert(writeTo);
    memcpy(writeTo, startFrom, newLength);
    writeTo[ newLength ] = 0;

    return writeTo;
}


enum wsFrameType wsParseHandshake(const uint8_t *inputFrame, size_t inputLength, struct handshake *hs)
{
    const char *inputPtr = (const char *)inputFrame;
    const char *endPtr = (const char *)inputFrame + inputLength;

    if (!strstr((const char *)inputFrame, "\r\n\r\n"))
        return WS_INCOMPLETE_FRAME;
    
    if (memcmp(inputFrame, (char *)("GET "), 4) != 0)
        return WS_ERROR_FRAME;
    // measure resource size
    char *first = strchr((const char *)inputFrame, ' ');
    if (!first)
        return WS_ERROR_FRAME;
    first++;
    char *second = strchr(first, ' ');
    if (!second)
        return WS_ERROR_FRAME;

    if (hs->resource)
    {
        free(hs->resource);
        hs->resource = NULL;
    }
    hs->resource = (char *)malloc(second - first + 1); // +1 is for \x00 symbol
    // assert(hs->resource);

    if (sscanf(inputPtr, (char *)("GET %s HTTP/1.1\r\n"), hs->resource) != 1)
        return WS_ERROR_FRAME;
    inputPtr = strstr(inputPtr, rn) + 2;

    /*
        parse next lines
     */
    #define prepare(x) do {if (x) { free(x); x = NULL; }} while(0)
    #define strtolower(x) do { int i; for (i = 0; x[i]; i++) x[i] = tolower(x[i]); } while(0)
    uint8_t connectionFlag = FALSE;
    uint8_t upgradeFlag = FALSE;
    uint8_t subprotocolFlag = FALSE;
    uint8_t versionMismatch = FALSE;
    while (inputPtr < endPtr && inputPtr[0] != '\r' && inputPtr[1] != '\n')
    {
        if (memcmp(inputPtr, hostField, strlen(hostField)) == 0)
        {
            inputPtr += strlen(hostField);
            prepare(hs->host);
            hs->host = getUptoLinefeed(inputPtr);
        } else
        if (memcmp(inputPtr, originField, strlen(originField)) == 0)
        {
            inputPtr += strlen(originField);
            prepare(hs->origin);
            hs->origin = getUptoLinefeed(inputPtr);
        } else
        if (memcmp(inputPtr, protocolField, strlen(protocolField)) == 0)
        {
            inputPtr += strlen(protocolField);
            subprotocolFlag = TRUE;
        } else
        if (memcmp(inputPtr, keyField, strlen(keyField)) == 0)
        {
            inputPtr += strlen(keyField);
            prepare(hs->key);
            hs->key = getUptoLinefeed(inputPtr);
        } else
        if (memcmp(inputPtr, versionField, strlen(versionField)) == 0)
        {
            inputPtr += strlen(versionField);
            char *versionString = NULL;
            versionString = getUptoLinefeed(inputPtr);
            if (memcmp(versionString, version, strlen(version)) != 0)
                versionMismatch = TRUE;
            free(versionString);
        } else
        if (memcmp(inputPtr, connectionField, strlen(connectionField)) == 0)
        {
            inputPtr += strlen(connectionField);
            char *connectionValue = NULL;
            connectionValue = getUptoLinefeed(inputPtr);
            strtolower(connectionValue);
            // assert(connectionValue);
            if (strstr(connectionValue, upgrade) != NULL)
                connectionFlag = TRUE;
            free(connectionValue);
        } else
        if (memcmp(inputPtr, upgradeField, strlen(upgradeField)) == 0)
        {
            inputPtr += strlen(upgradeField);
            char *compare = NULL;
            compare = getUptoLinefeed(inputPtr);
            strtolower(compare);
            // assert(compare);
            if (memcmp(compare, websocket, strlen(websocket)) == 0)
                upgradeFlag = TRUE;
            free(compare);
        };

        inputPtr = strstr(inputPtr, rn) + 2;
    }

    // we have read all data, so check them
    if (!hs->host || !hs->key || !connectionFlag || !upgradeFlag || subprotocolFlag
        || versionMismatch)
    {
        hs->frameType = WS_ERROR_FRAME;
    } else
    {
        hs->frameType = WS_OPENING_FRAME;
    }
    
    return hs->frameType;
}


void wsGetHandshakeAnswer(const struct handshake *hs, uint8_t *outFrame, size_t *outLength)
{
    // assert(outFrame && *outLength);
    // assert(hs->frameType == WS_OPENING_FRAME);
    // assert(hs && hs->key);

    char *responseKey = NULL;
    uint8_t length = strlen(hs->key) + strlen(secret);
    responseKey = malloc(length + 1);
    memcpy(responseKey, hs->key, strlen(hs->key));
    memcpy(&(responseKey[strlen(hs->key)]), secret, strlen(secret));
    responseKey[length] = '\0';
    unsigned char shaHash[20];
    memset(shaHash, 0, sizeof(shaHash));
    sha1(shaHash, responseKey, length);
    int base64Length = Base64encode(responseKey, shaHash, 20);
    responseKey[base64Length] = '\0';
    int written = sprintf((char *)outFrame,
                          (char *)("HTTP/1.1 101 Switching Protocols\r\n"
                                 "%s%s\r\n"
                                 "%s%s\r\n"
                                 "Sec-WebSocket-Accept: %s\r\n\r\n"),
                          upgradeField,
                          websocket,
                          connectionField,
                          upgrade2,
                          responseKey);
    
    free(responseKey);
    // if assert fail, that means, that we corrupt memory
    // assert(written <= *outLength);
    *outLength = written;
}


void wsMakeFrame(const uint8_t *data, size_t dataLength, uint8_t *outFrame, size_t *outLength, enum wsFrameType frameType)
{
    // assert(outFrame && *outLength);
    // assert(frameType < 0x10);
    //if (dataLength > 0)
    //    assert(data);
    
    outFrame[0] = 0x80 | frameType;
    
    if (dataLength <= 125)
    {
        outFrame[1] = dataLength;
        *outLength = 2;
    }
    else if (dataLength <= 0xFFFF)
    {
        outFrame[1] = 126;
        uint16_t payloadLength16b = htons(dataLength);
        memcpy(&outFrame[2], &payloadLength16b, 2);
        *outLength = 4;
    }
    else
    {
        // assert(dataLength <= 0xFFFF);
        
        /* implementation for 64bit systems
        outFrame[1] = 127;
        dataLength = htonll(dataLength);
        memcpy(&outFrame[2], &dataLength, 8);
        *outLength = 10;
        */
    }
    memcpy(&outFrame[*outLength], data, dataLength);
    *outLength+= dataLength;
}

static size_t getPayloadLength(const uint8_t *inputFrame, size_t inputLength, uint8_t *payloadFieldExtraBytes, enum wsFrameType *frameType)
{
    size_t payloadLength = inputFrame[1] & 0x7F;
    *payloadFieldExtraBytes = 0;
    if ((payloadLength == 0x7E && inputLength < 4) || (payloadLength == 0x7F && inputLength < 10))
    {
        *frameType = WS_INCOMPLETE_FRAME;
        return 0;
    }
    if (payloadLength == 0x7F && (inputFrame[3] & 0x80) != 0x0)
    {
        *frameType = WS_ERROR_FRAME;
        return 0;
    }

    if (payloadLength == 0x7E)
    {
        uint16_t payloadLength16b = 0;
        *payloadFieldExtraBytes = 2;
        memcpy(&payloadLength16b, &inputFrame[2], *payloadFieldExtraBytes);
        payloadLength = ntohs(payloadLength16b);
    }
    else if (payloadLength == 0x7F)
    {
        *frameType = WS_ERROR_FRAME;
        return 0;
        
        /* // implementation for 64bit systems
        uint64_t payloadLength64b = 0;
        *payloadFieldExtraBytes = 8;
        memcpy(&payloadLength64b, &inputFrame[2], *payloadFieldExtraBytes);
        if (payloadLength64b > SIZE_MAX) {
            *frameType = WS_ERROR_FRAME;
            return 0;
        }
        payloadLength = (size_t)ntohll(payloadLength64b);
        */
    }

    return payloadLength;
}


enum wsFrameType wsParseInputFrame(uint8_t *inputFrame, size_t inputLength, uint8_t **dataPtr, size_t *dataLength)
{
    // assert(inputFrame && inputLength);

    if (inputLength < 2)
        return WS_INCOMPLETE_FRAME;

    if ((inputFrame[0] & 0x70) != 0x0) // checks extensions off
    {
        return WS_ERROR_FRAME;
    }
    if ((inputFrame[0] & 0x80) != 0x80) // we haven't continuation frames support
    {
        return WS_ERROR_FRAME; // so, fin flag must be set
    }
    if ((inputFrame[1] & 0x80) != 0x80) // checks masking bit
    {
        return WS_ERROR_FRAME;
    }

    uint8_t opcode = inputFrame[0] & 0x0F;
    if (opcode == WS_TEXT_FRAME ||
            opcode == WS_BINARY_FRAME ||
            opcode == WS_CLOSING_FRAME ||
            opcode == WS_PING_FRAME ||
            opcode == WS_PONG_FRAME
    )
    {
        enum wsFrameType frameType = opcode;

        uint8_t payloadFieldExtraBytes = 0;
        size_t payloadLength = getPayloadLength(inputFrame, inputLength, &payloadFieldExtraBytes, &frameType);
        // printf("op=%d len=%d\n", opcode, inputLength);
        if (payloadLength > 0)
        {
            if (payloadLength + 6 + payloadFieldExtraBytes > inputLength) // 4-maskingKey, 2-header
                return WS_INCOMPLETE_FRAME;
            uint8_t *maskingKey = &inputFrame[2 + payloadFieldExtraBytes];

            // assert(payloadLength == inputLength - 6 - payloadFieldExtraBytes);

            *dataPtr = &inputFrame[2 + payloadFieldExtraBytes + 4];
            *dataLength = payloadLength;
        
            size_t i;
            for (i = 0; i < *dataLength; i++)
            {
                (*dataPtr)[i] = (*dataPtr)[i] ^ maskingKey[i%4];
            }
        }
        return frameType;
    }
    else
    {
        printf("Unknown F0:%02X\n", inputFrame[0]);
    }

    return WS_ERROR_FRAME;
}
