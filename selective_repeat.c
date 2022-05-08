#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER 1080 /* 4*270 */
#define ACK_TIMER 1080 /* 4*270 */
#define MAX_SEQ 7
#define NR_BUFS ((MAX_SEQ + 1) >> 1)
#define inc(x) x = (x == MAX_SEQ) ? 0 : x + 1;

typedef unsigned char seq_nr;
typedef unsigned char frame_kind;
typedef unsigned char packet[PKT_LEN];
typedef unsigned char boolean;

struct frame {
	frame_kind kind; /* FRAME_DATA */
	seq_nr ack;
	seq_nr seq;
	packet info;
	unsigned int checksum;
};

static int phl_ready = 0;
boolean no_nak = 1;

static unsigned char between(seq_nr a, seq_nr b, seq_nr c) {
	return((a <= b && b < c) || (a <= b && c < a) || (b < c && c < a));
}

static void put_frame(unsigned char *frame, int len) {
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

static void send_data(frame_kind fk, seq_nr frame_nr, seq_nr frame_expected, packet buffer[]) {
	struct frame s;
	s.kind = fk;
	s.seq = frame_nr;
	/* ack responses to the previous frame */
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	/* === (frame_expected - 1) mod (MAX_SEQ + 1) */

	stop_ack_timer();
    if(fk == FRAME_DATA) {
		memcpy(s.info, buffer[frame_nr % NR_BUFS], PKT_LEN);
		dbg_frame("Send DATA %d %d, group ID %d\n", s.seq, s.ack, *(short *)s.info);
        put_frame((unsigned char *)&s, PKT_LEN + 3);
        start_timer(frame_nr, DATA_TIMER);
	} else if(fk == FRAME_ACK) {
		dbg_frame("Send ACK %d\n", s.ack);
		/* needless to adding info segment to short frame */
		put_frame((unsigned char *)&s, 3);
	} else if(fk == FRAME_NAK) {
        dbg_frame("Send NAK %d\n", s.ack);
		/* needless to adding info segment to short frame */
        put_frame((unsigned char*)&s, 3);
	}
}

int main(int argc, char **argv) {
	int event, arg;
	int len = 0;

	struct frame r;
	seq_nr next_frame_to_send;
	seq_nr frame_expected;
	seq_nr ack_expected;
    seq_nr too_far;
    
	packet in_buf[NR_BUFS];
	packet out_buf[NR_BUFS];
    boolean arrived[NR_BUFS];

	seq_nr nbuffered;

	protocol_init(argc, argv);
	lprintf("Designed by Wu Qingliu, build: "__DATE__"  "__TIME__"\n");

	disable_network_layer();

	next_frame_to_send = 0;
	frame_expected = 0;
	ack_expected = 0;
    too_far = NR_BUFS;
	nbuffered = 0;

    for(int i = 0; i < NR_BUFS; i++)
	arrived[i] = 0;

	for (;;) {
		event = wait_for_event(&arg);

		switch (event) {
		case NETWORK_LAYER_READY:
			get_packet(out_buf[next_frame_to_send % NR_BUFS]);
			nbuffered++;
			send_data(FRAME_DATA, next_frame_to_send, frame_expected, out_buf);
			inc(next_frame_to_send);
			break;

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&r, sizeof r);
			/* the minimum length with crc checksum is 7 bytes */
			if (len < 7 || crc32((unsigned char *)&r, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				break;
			}

			if (r.kind == FRAME_DATA) {
				dbg_frame("Recv DATA %d %d, ID %d, %d bytes\n", r.seq, r.ack,
						*(short *)r.info, len);
                if((r.seq != frame_expected && no_nak)) {
                    dbg_frame("Send NAK %d\n", frame_expected);
					no_nak = 0;
                    /* send nak */
                    send_data(FRAME_NAK, 0, frame_expected, out_buf);
				} else {
					start_ack_timer(ACK_TIMER);
				}

                if(between(frame_expected, r.seq, too_far) && !arrived[r.seq % NR_BUFS]) {
                    arrived[r.seq % NR_BUFS] = 1;
                    memcpy(in_buf[r.seq % NR_BUFS], r.info, PKT_LEN);

                    while(arrived[frame_expected % NR_BUFS]) {
/*                         dbg_frame("Put DATA %d %d, ID %d to network layer\n", r.seq, r.ack,
								*(short *)r.info); */
						put_packet(in_buf[frame_expected % NR_BUFS], PKT_LEN);
                        no_nak = 1;
						arrived[frame_expected % NR_BUFS] = 0;
						inc(frame_expected);
						inc(too_far);
						start_ack_timer(ACK_TIMER);
					}
				} else {
					dbg_frame("Frame with wrong seq_nr %d received\n", r.seq);
				}
			}

			if (r.kind == FRAME_ACK)
				dbg_frame("Recv ACK %d\n", r.ack);
			
			if((r.kind == FRAME_NAK) && between(ack_expected, (r.ack+1) % (MAX_SEQ+1),
				next_frame_to_send)) {
				dbg_frame("Recv NAK %d, resend DATA %d %d\n", r.ack, (r.ack+1) % (MAX_SEQ+1),
						frame_expected);
					send_data(FRAME_DATA, (r.ack+1) % (MAX_SEQ+1), frame_expected, out_buf);
				}
				
			while(between(ack_expected, r.ack, next_frame_to_send)) {
				nbuffered--;
				stop_timer(ack_expected);
				inc(ack_expected);
			}
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			send_data(FRAME_DATA, arg, frame_expected, out_buf);
			break;

		/* No data to send from network layer until ack timer expired. */
		case ACK_TIMEOUT:
			/* next_frame_to_send and buffer is meaningless */
			send_data(FRAME_ACK, 0, frame_expected, out_buf);
			break;
		
		default:
			dbg_event("Unhandled cased");
			break;
		}

		if (nbuffered < NR_BUFS && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}