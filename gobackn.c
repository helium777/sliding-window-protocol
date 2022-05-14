#include <stdbool.h>
#include "common.h"
#include "protocol.h"

static bool phl_ready = false;

// 返回 a <= b < c (循环)
static bool between(seq_t a, seq_t b, seq_t c) {
    return ((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a));
}

static bool crc32_check(void *data, int len) {
    if (crc32((uchar_t *) data, len) != 0) {
        return false;
    }
    return true;
}

// 生成 CRC 校验并发送到物理层
static void put_frame(uchar_t *frame, int len) {
    *(uint32_t *) (frame + len) = crc32(frame, len);

    send_frame(frame, len + 4);  // sizeof(uint32_t) = 4
    phl_ready = false;
}

static void send_data_frame(seq_t frame_nr, seq_t frame_expected, packet_t buffer[]) {
    struct frame f = {
            .kind = FRAME_DATA,
            .ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1),  // piggyback ack
            .seq = frame_nr,
    };
    memcpy(f.data, buffer[frame_nr], sizeof(f.data));

    dbg_frame("Sending DATA frame <ack=%d, seq=%d, id=%d>\n", f.ack, f.seq, *(short *) f.data);

    put_frame((uint8_t *) &f, 3 + sizeof(f.data));
    start_timer(frame_nr, DATA_TIMEOUT_MS);
    stop_ack_timer();
}

static void send_ack_frame(seq_t ack_nr) {
    struct frame f = {
            .kind = FRAME_ACK,
            .ack = ack_nr,
    };

    dbg_frame("Sending ACK frame <ack=%d>\n", f.ack);

    put_frame((uint8_t *) &f, 2);  // 2 = sizeof(f.kind) + sizeof(f.ack)
}

int main(int argc, char **argv) {
    int arg;  // *_TIMEOUT 事件中产生超时事件的定时器编号
    int event;
    int len;  // 接收到的数据帧长度

    // 发送窗口
    seq_t next_frame_to_send = 0;
    seq_t ack_expected = 0;

    // 接收窗口
    seq_t frame_expected = 0;

    struct frame f;  // 接收到的帧
    packet_t buffer[MAX_SEQ + 1];  // 出境 buffer
    seq_t buffer_len = 0;  // 当前 buffer 中的 packet 数

    protocol_init(argc, argv);
    lprintf("Go back N protocol by Yin Rui, build %s %s\n", __DATE__, __TIME__);

    disable_network_layer();

    while (true) {
        event = wait_for_event(&arg);

#ifndef NDEBUG
        if (event != PHYSICAL_LAYER_READY) {
            lprintf("ack = %d, next = %d, inbound = %d\n", ack_expected, next_frame_to_send,
              frame_expected);
            lprintf("buffer = %d\n", buffer_len);
        }
#endif

        switch (event) {
            case NETWORK_LAYER_READY:
                get_packet(buffer[next_frame_to_send]);
                buffer_len++;
                send_data_frame(next_frame_to_send, frame_expected, buffer);
                inc(next_frame_to_send);
                break;

            case PHYSICAL_LAYER_READY:
                phl_ready = true;
                break;

            case FRAME_RECEIVED:
                len = recv_frame((uchar_t *) &f, sizeof(f));
                if (f.kind == FRAME_ACK) {
                    dbg_frame("Received ACK frame <ack=%d>\n", f.ack);
                } else if (f.kind == FRAME_DATA) {
                    dbg_frame("Received DATA frame <ack=%d, seq=%d, id=%d>\n", f.ack, f.seq,
                              *(short *) f.data);
                }

                if (!crc32_check(&f, len)) {
                    dbg_event("*** Bad CRC Checksum ***\n");
                    break;  // 忽略错误帧
                }

                if (f.kind == FRAME_DATA) {
                    if (f.seq == frame_expected) {
                        put_packet(f.data, sizeof(f.data));
                        start_ack_timer(ACK_TIMEOUT_MS);
                        inc(frame_expected);
                    }
                }

                // 累积确认
                while (between(ack_expected, f.ack, next_frame_to_send)) {
                    stop_timer(ack_expected);
                    buffer_len--;
                    inc(ack_expected);
                }

                break;

            case DATA_TIMEOUT:
                dbg_event("DATA frame <seq=%d> timeout\n", arg);

                next_frame_to_send = ack_expected;
                for (seq_t i = 0; i < buffer_len; i++) {
                    send_data_frame(next_frame_to_send, frame_expected, buffer);
                    inc(next_frame_to_send);
                }
                break;

            case ACK_TIMEOUT:
                send_ack_frame((frame_expected + MAX_SEQ) % (MAX_SEQ + 1));
                stop_ack_timer();
                break;

            default:
                break;
        }

        if (buffer_len < MAX_SEQ && phl_ready) {
            enable_network_layer();
        } else {
            disable_network_layer();
        }
    }
}