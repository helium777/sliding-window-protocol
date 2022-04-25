#pragma once

#include <stdint-gcc.h>
#include "protocol.h"

// frame.kind

#define FRAME_DATA 0
#define FRAME_ACK  1
#define FRAME_NAK  2

// 最大 seq 值, seq = 0, 1, ..., MAX_SEQ
#define MAX_SEQ 7

// 数据帧超时时间
#define DATA_TIMEOUT_MS 2000

// piggyback ack 延迟超时时间
#define ACK_DELAY_TIMEOUT_MS 500

typedef uint8_t packet_t[PKT_LEN];

typedef uint8_t seq_t;

typedef uint8_t uchar_t;

struct frame {
    uchar_t kind;
    seq_t ack;
    seq_t seq;
    packet_t data;
    uint32_t crc32;
};

#define inc(x) if ((x) < MAX_SEQ) (x)++; else (x) = 0