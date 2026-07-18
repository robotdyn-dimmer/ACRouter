/**
 * @file espnow_proto.h
 * @brief RBgrid ESP-NOW wire protocol — canonical frame definitions (proto_ver 1).
 *        Source of truth: documents/architecture/ESPNOW_PROTOCOL.md (in the hub repo).
 *        Shared by hub + node; keep BOTH copies in sync and bump RBN_PROTO_VER on any
 *        breaking shape change (additive fields go at the end of a record).
 *
 * Little-endian (ESP32 native). All multi-byte fields are LE. Structs are packed.
 */
#ifndef RBGRID_ESPNOW_PROTO_H
#define RBGRID_ESPNOW_PROTO_H

#include <stdint.h>

#define RBN_MAGIC0     0x42  /* 'B' */
#define RBN_MAGIC1     0x52  /* 'R'  -> wire bytes 0x42 0x52 ("BR" LE of "RB") */
#define RBN_PROTO_VER  1

/* msg_type */
enum {
    RBN_MSG_HELLO         = 0x01,
    RBN_MSG_REALTIME      = 0x02,
    RBN_MSG_PERIOD        = 0x03,
    RBN_MSG_PERIOD_ACK    = 0x04,
    RBN_MSG_NODE_STATS    = 0x05,  /* node->hub: link/sync health (so a no-COM node can be read via the hub) */
    RBN_MSG_PAIR_REQ      = 0x10,
    RBN_MSG_PAIR_ACK      = 0x11,
    RBN_MSG_TIME_REQ      = 0x20,
    RBN_MSG_TIME_RESP     = 0x21,
    RBN_MSG_SYNC_BEACON   = 0x22,
    RBN_MSG_SYNC_FOLLOWUP = 0x23,
    RBN_MSG_CONFIG        = 0x30,
    RBN_MSG_CONFIG_ACK    = 0x31,
    RBN_MSG_LATCH_NOW     = 0x32,  /* reserved v2 */
    RBN_MSG_SET_OUTPUT    = 0x40,  /* hub->node: drive an output (dimmer/relay), encrypted unicast */
    RBN_MSG_OUTPUT_STATE  = 0x41,  /* node->hub: applied-state + ACK (held-until-ACK on hub) */
};

/* Common 12-byte header (every frame begins with this). */
typedef struct __attribute__((packed)) {
    uint8_t  magic0;     /* RBN_MAGIC0 */
    uint8_t  magic1;     /* RBN_MAGIC1 */
    uint8_t  proto_ver;  /* RBN_PROTO_VER */
    uint8_t  msg_type;
    uint32_t seq;        /* per-sender monotonic */
    uint32_t node_ts;    /* sender Unix-sec clock (0 if not yet time-synced) */
} rbn_hdr_t;

/* 0x22 SYNC_BEACON (hub->broadcast): body = beacon_id + epoch.
 * epoch = a boot-unique id of the master/hub. When it changes (hub rebooted/OTA'd, beacon_id restarts
 * from 1 on a fresh esp_timer base), the node MUST reset its discipline servo — otherwise the old-epoch
 * samples poison the regression for ~one window. */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint32_t  beacon_id;
    uint32_t  epoch;
    uint8_t   hub_channel;   /* the hub's exact WiFi channel — the node sets its radio to this (rx_ctrl can
                              * report an adjacent overlapping channel, so trust the hub's own value). */
} rbn_sync_beacon_t;

/* 0x23 SYNC_FOLLOWUP (hub->broadcast): body = beacon_id + the hub's esp_timer (us) at beacon TX. */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint32_t  beacon_id;
    uint64_t  hub_tx_us;   /* esp_timer_get_time() captured in the hub's send callback */
} rbn_sync_followup_t;

/* 0x02 REALTIME record (one per channel). */
typedef struct __attribute__((packed)) {
    uint8_t  channel_id;   /* 0..N; 0xFF = node voltage bus */
    float    v_rms;
    float    i_rms;
    float    p_active;     /* signed: + import / - export */
    int16_t  pf_x1000;
    uint16_t freq_x100;
    uint8_t  flags;        /* bit0 valid, bit1 direction_export */
} rbn_rt_rec_t;

/* 0x02 REALTIME frame: header + rec_count + rec_count*records. */
typedef struct __attribute__((packed)) {
    rbn_hdr_t   h;
    uint8_t     rec_count;
    rbn_rt_rec_t recs[];   /* flexible */
} rbn_realtime_t;

/* 0x10 PAIR_REQ (node->hub, broadcast while unpaired): the node announces its identity + channels so the
 * hub (in its pairing window) can register it into the node registry.
 * B3a: an 8-byte HMAC auth tag (rbn_pair_tag, keyed by the shared pairing key) trails the chans[] array —
 *   the hub rejects a PAIR_REQ whose tag doesn't verify, so only a node holding the key can self-register.
 *   Wire layout: [hdr][fw_ver][chan_count][chans × chan_count][tag(8)]. The tag covers everything before it. */
typedef struct __attribute__((packed)) { uint8_t channel_id; uint8_t ct_code; } rbn_pair_chan_t;
typedef struct __attribute__((packed)) {
    rbn_hdr_t       h;
    uint16_t        fw_ver;
    uint8_t         chan_count;
    rbn_pair_chan_t chans[];   /* chan_count × {node flat channel_id, ct_code 1/2/3/4/6}, THEN uint8_t tag[8] */
} rbn_pair_req_t;
/* Byte length of a PAIR_REQ carrying n channels + the trailing auth tag. */
#define RBN_PAIR_REQ_LEN(n)        (sizeof(rbn_pair_req_t) + (size_t)(n) * sizeof(rbn_pair_chan_t) + 8)
/* Length of the tag-covered prefix (everything except the trailing 8-byte tag). */
#define RBN_PAIR_REQ_PREFIX_LEN(n) (sizeof(rbn_pair_req_t) + (size_t)(n) * sizeof(rbn_pair_chan_t))

/* 0x11 PAIR_ACK (hub->node): accepted=1 → the hub registered the node (units activate on its next reboot). */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint8_t   accepted;
} rbn_pair_ack_t;

/* 0x20 TIME_REQ (node->hub): request the hub's wall-clock. body = req_token (echoed back for RTT). */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint32_t  req_token;
} rbn_time_req_t;

/* 0x21 TIME_RESP (hub->node): body = req_token + the hub's Unix time (ms). Node anchors its wall-clock
 * to this (+ RTT/2) and tracks it with the disciplined clock rate → calendar-aligned period boundaries. */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint32_t  req_token;
    uint64_t  hub_unix_ms;
} rbn_time_resp_t;

/* 0x05 NODE_STATS (node->hub): link + sync health, so a node with no COM can be read via the hub.
 * loss is inferable: max_beacon_id is monotonic from 1, so missed = max_beacon_id - beacons_recv. */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint32_t  beacons_recv;
    uint32_t  followups_recv;
    uint32_t  max_beacon_id;
    uint32_t  fits;             /* residual sample count */
    float     resid_mean_us;
    float     resid_sd_us;
    float     resid_maxabs_us;
    float     skew_ppm;
} rbn_node_stats_t;

/* 0x03 PERIOD record (one per channel — a finished, on-node-integrated period for billing). */
typedef struct __attribute__((packed)) {
    uint8_t  channel_id;
    uint8_t  period_type;    /* rbpower_stat_period_type_t */
    uint32_t period_ms;      /* integrated window length (the energy dt) */
    float    avg_power_w;
    float    max_power_w;
    float    energy_wh;
    float    voltage_v;      /* representative period voltage (0 = unknown / current-only) */
    float    current_avg_a;
    float    current_min_a;
    float    current_max_a;
} rbn_period_rec_t;

/* 0x03 PERIOD frame (node->hub): header (h.seq identifies the frame for the ACK) + records.
 * The node HOLDS each frame until a matching PERIOD_ACK (held-until-ACK) so billing energy is never
 * lost to a dropped frame. */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint8_t   rec_count;
    rbn_period_rec_t recs[];
} rbn_period_t;

/* 0x04 PERIOD_ACK (hub->node): body = the PERIOD frame's h.seq being acknowledged. */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint32_t  ack_seq;
} rbn_period_ack_t;

/* 0x32 LATCH_NOW (hub->node, C / v2): the hub commands the node to immediately close its current period and
 * emit a PERIOD frame, then restart accumulation — so a period boundary coincides with a tariff-ZONE boundary
 * (which the node can't know; tariff zones live only in the master). The hub sends this on a zone transition
 * (auto) or on demand (verb). Encrypted unicast (the node is a registered peer). */
enum { RBN_LATCH_REASON_MANUAL = 0, RBN_LATCH_REASON_ZONE = 1 };
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint8_t   reason;        /* RBN_LATCH_REASON_* */
    uint32_t  boundary_unix; /* hub wall-clock second of the boundary (reference/logging) */
} rbn_latch_now_t;

/* ================================================================
 * OUTPUT NODES (dimmer / relay) — beacon-independent, works with an open-RX hub.
 * Discovery = HELLO; control = SET_OUTPUT (encrypted unicast); liveness/failsafe =
 * SET_OUTPUT-silence (NOT beacon-silence). Canonical with rbgrid node repo.
 * ================================================================ */

/* node family (HELLO classifies by MAC; family selects the driver on the hub) */
enum { RBN_FAMILY_SENSOR = 0x01, RBN_FAMILY_DIMMER = 0x02, RBN_FAMILY_RELAY = 0x03 };

/* HELLO flags */
#define RBN_HELLO_F_HAS_VOLTAGE_HW 0x01
#define RBN_HELLO_F_MAINS_POWERED  0x02
#define RBN_HELLO_F_TIME_SYNCED    0x04
#define RBN_HELLO_F_HAS_OUTPUTS    0x08

/* output kind */
enum { RBN_OUT_KIND_DIMMER = 0x01, RBN_OUT_KIND_RELAY = 0x02 };

/* per-output capability descriptor (in HELLO). dimmer: range 0..1000 (‰); relay: 0..1. */
typedef struct __attribute__((packed)) {
    uint8_t  output_id;
    uint8_t  kind;            /* RBN_OUT_KIND_* */
    uint16_t range_min;
    uint16_t range_max;
    uint16_t failsafe_value;  /* value each output falls to on hub-silence (relay OFF=0, dimmer 0) */
} rbn_out_cap_t;              /* 8 bytes */

/* 0x01 HELLO (node->hub, broadcast-OPEN ~30s): identity + capability. Registry key = MAC. */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint16_t  fw_ver;
    uint16_t  hw_model;
    uint8_t   family;         /* RBN_FAMILY_* */
    uint8_t   flags;          /* RBN_HELLO_F_* */
    uint8_t   channel_count;  /* sensor inputs (0 on a pure output node) */
    uint8_t   out_count;      /* number of rbn_out_cap_t following */
    rbn_out_cap_t out_cap[];  /* out_count descriptors */
} rbn_hello_t;

/* 0x40 SET_OUTPUT (hub->node, encrypted unicast): h.seq = command id (echoed in ACK).
 * Idempotent by (output_id,value): a repeat is safe AND serves as failsafe keep-alive. */
#define RBN_SETOUT_F_SET_FAILSAFE 0x01   /* also store value as this output's failsafe default */
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint8_t   output_id;
    uint8_t   kind;           /* node rejects on mismatch (result=3) */
    uint16_t  value;          /* dimmer 0..1000‰ / relay 0|1 (clamped to range) */
    uint16_t  ramp_ms;        /* dimmer fade to value (0=immediate); relay: ignored */
    uint8_t   flags;          /* RBN_SETOUT_F_* */
} rbn_set_output_t;

/* 0x41 OUTPUT_STATE (node->hub, encrypted unicast): ACK + applied state. */
#define RBN_OUTSTATE_F_FAILSAFE_ACTIVE 0x01
#define RBN_OUTSTATE_F_LOCAL_OVERRIDE  0x02
typedef struct __attribute__((packed)) {
    rbn_hdr_t h;
    uint8_t   output_id;
    uint8_t   kind;
    uint16_t  value;          /* value actually applied now */
    uint32_t  ack_seq;        /* the SET_OUTPUT h.seq (0 = unsolicited: manual override / failsafe trip) */
    uint8_t   flags;          /* RBN_OUTSTATE_F_* */
    uint8_t   result;         /* 0 OK · 1 clamped · 2 unknown output_id · 3 kind mismatch */
} rbn_output_state_t;

/* Node watchdog: no authenticated hub frame within this → each output → its failsafe_value.
 * The hub MUST re-assert desired outputs at <= RBN_OUTPUT_FAILSAFE_MS/2 to hold a non-failsafe state. */
#define RBN_OUTPUT_FAILSAFE_MS 5000

static inline void rbn_hdr_init(rbn_hdr_t *h, uint8_t type, uint32_t seq, uint32_t node_ts)
{
    h->magic0 = RBN_MAGIC0; h->magic1 = RBN_MAGIC1; h->proto_ver = RBN_PROTO_VER;
    h->msg_type = type; h->seq = seq; h->node_ts = node_ts;
}

static inline int rbn_hdr_ok(const rbn_hdr_t *h)
{
    return h->magic0 == RBN_MAGIC0 && h->magic1 == RBN_MAGIC1 && h->proto_ver == RBN_PROTO_VER;
}

#endif /* RBGRID_ESPNOW_PROTO_H */
