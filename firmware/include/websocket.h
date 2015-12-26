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

#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#ifdef  __cplusplus
extern "C" {
#endif


#ifndef TRUE
    #define TRUE 1
#endif
#ifndef FALSE
    #define FALSE 0
#endif

static const char connectionField[] = "Connection: ";
static const char upgrade[] = "upgrade";
static const char upgrade2[] = "Upgrade";
static const char upgradeField[] = "Upgrade: ";
static const char websocket[] = "websocket";
static const char hostField[] = "Host: ";
static const char originField[] = "Origin: ";
static const char keyField[] = "Sec-WebSocket-Key: ";
static const char protocolField[] = "Sec-WebSocket-Protocol: ";
static const char versionField[] = "Sec-WebSocket-Version: ";
static const char version[] = "13";
static const char secret[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

enum wsFrameType // errors starting from 0xF0
{
    WS_EMPTY_FRAME = 0xF0,
    WS_ERROR_FRAME = 0xF1,
    WS_INCOMPLETE_FRAME = 0xF2,
    WS_TEXT_FRAME = 0x01,
    WS_BINARY_FRAME = 0x02,
    WS_PING_FRAME = 0x09,
    WS_PONG_FRAME = 0x0A,
    WS_OPENING_FRAME = 0xF3,
    WS_CLOSING_FRAME = 0x08
};


enum wsState
{
    WS_STATE_OPENING,
    WS_STATE_NORMAL,
    WS_STATE_CLOSING
};


struct handshake
{
    char *host;
    char *origin;
    char *key;
    char *resource;
    enum wsFrameType frameType;
};

/**
 * @param inputFrame Pointer to input frame
 * @param inputLength Length of input frame
 * @param hs Cleared with nullHandshake() handshake structure
 * @return Type of parsed frame
 */
enum wsFrameType wsParseHandshake(const uint8_t *inputFrame, size_t inputLength, struct handshake *hs);
    
/**
 * @param hs Filled handshake structure
 * @param outFrame Pointer to frame buffer
 * @param outLength Length of frame buffer. Return length of out frame
 */
void wsGetHandshakeAnswer(const struct handshake *hs, uint8_t *outFrame, size_t *outLength);

/**
 * @param data Pointer to input data array
 * @param dataLength Length of data array
 * @param outFrame Pointer to frame buffer
 * @param outLength Length of out frame buffer. Return length of out frame
 * @param frameType [WS_TEXT_FRAME] frame type to build
 */
void wsMakeFrame(const uint8_t *data, size_t dataLength, uint8_t *outFrame, size_t *outLength, enum wsFrameType frameType);

/**
 *
 * @param inputFrame Pointer to input frame. Frame will be modified.
 * @param inputLen Length of input frame
 * @param outDataPtr Return pointer to extracted data in input frame
 * @param outLen Return length of extracted data
 * @return Type of parsed frame
 */
enum wsFrameType wsParseInputFrame(uint8_t *inputFrame, size_t inputLength, uint8_t **dataPtr, size_t *dataLength);

/**
 * @param hs NULL handshake structure
 */
void nullHandshake(struct handshake *hs);

/**
 * @param hs free and NULL handshake structure
 */
void freeHandshake(struct handshake *hs);

#ifdef  __cplusplus
}
#endif

#endif  /* WEBSOCKET_H */
