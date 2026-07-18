/**
 * @file esp_now_source.c
 * @brief ESP-NOW rbAmp measurement receiver → Sensor Hub. See esp_now_source.h.
 *
 * Pattern mirrors rbgrid's esp_now_hub.c (recv-cb stashes under a portMUX, an
 * inject task drains + posts off-callback) but stripped to the minimum: RX
 * REALTIME only, open (no crypto), no PTP/beacon/period. Maps a node's primary
 * channel to a Sensor Hub role exactly like rbamp_source's publish_snapshot.
 */
#include "esp_now_source.h"
#include "espnow_proto.h"
#include <math.h>          // isfinite() — drop NaN/Inf arriving on the wire

#include "sdkconfig.h"
#include "acrouter_events.h"
#include "acrouter_measurements.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "espnow_src";

#define ESPNOW_SRC_NVS_NS        "espnow_src"
#define ESPNOW_PRESENCE_TO_MS    2000   /* node considered offline after this w/o REALTIME */
#define ESPNOW_INJECT_MS         200    /* inject cadence */

/* ---- seen-node table (written by recv-cb, drained by inject task) ---- */
typedef struct {
    bool     used;
    uint8_t  mac[6];
    float    i, v, p, pf, freq;
    bool     has_v;
    int64_t  last_us;
    bool     fresh;
} seen_t;

static seen_t s_seen[ESP_NOW_SOURCE_MAX_NODES];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* ---- node→role registry (commissioning) ---- */
typedef struct { bool used; uint8_t mac[6]; uint8_t role; } node_role_t;
static node_role_t s_node_role[ESP_NOW_SOURCE_MAX_NODES];

/* ---- output-node table (dimmer/relay over ESP-NOW) ----
 * Written by recv-cb (HELLO/OUTPUT_STATE) and the inject task (desired/keep-alive);
 * guarded by s_mux. ESP-NOW send/add_peer are done OUTSIDE the critical section. */
#define ESPNOW_OUT_KEEPALIVE_MS (RBN_OUTPUT_FAILSAFE_MS / 2)   /* re-assert cadence (2500ms) */
#define ESPNOW_OUT_OFFLINE_MS   (RBN_OUTPUT_FAILSAFE_MS + 1000)/* node offline w/o any frame */
typedef struct {
    bool     used;
    uint8_t  mac[6];
    uint8_t  family;
    uint16_t hw_model;
    uint8_t  out_count;
    rbn_out_cap_t caps[ESP_NOW_SOURCE_OUT_PER_NODE];
    bool     desired_set[ESP_NOW_SOURCE_OUT_PER_NODE];
    uint16_t desired_val[ESP_NOW_SOURCE_OUT_PER_NODE];
    uint16_t desired_ramp[ESP_NOW_SOURCE_OUT_PER_NODE];
    uint16_t applied_val[ESP_NOW_SOURCE_OUT_PER_NODE];
    uint8_t  last_result[ESP_NOW_SOURCE_OUT_PER_NODE];
    uint32_t last_ack_seq;
    bool     failsafe;
    int64_t  last_frame_us;   /* any HELLO/OUTPUT_STATE */
    int64_t  last_cmd_us;     /* last SET_OUTPUT we sent */
    bool     peer_added;
} out_node_t;
static out_node_t s_out[ESP_NOW_SOURCE_OUT_NODES_MAX];
static uint32_t   s_out_seq = 1;

static bool          s_initialized = false;
static volatile bool s_running     = false;
static TaskHandle_t  s_inject_task  = NULL;

/* ---- helpers ---- */

static bool mac_eq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

/* find/alloc a seen slot for a MAC — call under s_mux. */
static seen_t *seen_slot(const uint8_t mac[6])
{
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++)
        if (s_seen[i].used && mac_eq(s_seen[i].mac, mac)) return &s_seen[i];
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++)
        if (!s_seen[i].used) { s_seen[i].used = true; memcpy(s_seen[i].mac, mac, 6); return &s_seen[i]; }
    /* Table full — evict the oldest entry past the presence timeout, so a permanently
     * offline node never blocks a live one and junk MACs can't starve the fleet (D11).
     * If every slot is still live, drop this frame rather than kick an active node. */
    const int64_t now = esp_timer_get_time();
    int oldest = -1; int64_t oldest_us = 0;
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++) {
        if ((now - s_seen[i].last_us) <= (int64_t)ESPNOW_PRESENCE_TO_MS * 1000) continue;
        if (oldest < 0 || s_seen[i].last_us < oldest_us) { oldest = i; oldest_us = s_seen[i].last_us; }
    }
    if (oldest < 0) return NULL;
    memset(&s_seen[oldest], 0, sizeof(s_seen[oldest]));
    s_seen[oldest].used = true;
    memcpy(s_seen[oldest].mac, mac, 6);
    return &s_seen[oldest];
}

static esp_now_source_role_t role_for_mac(const uint8_t mac[6])
{
    /* Read the commissioning table under s_mux — set_role (web/serial task) mutates
     * it field-by-field while the inject/get_nodes readers walk it (D3). Callers reach
     * this OUTSIDE any critical section, so taking s_mux here does not nest. */
    esp_now_source_role_t r = ESPNOW_ROLE_NONE;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++)
        if (s_node_role[i].used && mac_eq(s_node_role[i].mac, mac)) {
            r = (esp_now_source_role_t)s_node_role[i].role;
            break;
        }
    portEXIT_CRITICAL(&s_mux);
    return r;
}

static int slot_channel_for_role(esp_now_source_role_t role)
{
    switch (role) {
        case ESPNOW_ROLE_GRID:  return ACROUTER_CH_GRID;
        case ESPNOW_ROLE_SOLAR: return ACROUTER_CH_SOLAR;
        case ESPNOW_ROLE_LOAD:  return ACROUTER_CH_LOAD;
        default:                return -1;
    }
}

/* Build an acrouter_measurements_t from one node's primary-channel sample and
 * post it. Same role/power/voltage gating as rbamp_source. */
static void post_node(const seen_t *s, esp_now_source_role_t role, uint8_t node_idx)
{
    if (role == ESPNOW_ROLE_NONE) return;

    acrouter_measurements_t meas;
    acrouter_measurements_init(&meas);
    meas.source       = ACROUTER_SOURCE_ESPNOW;
    meas.source_id    = node_idx;
    meas.timestamp_us = esp_timer_get_time();

    bool any = false;
    const int ch = slot_channel_for_role(role);
    if (ch >= 0 && isfinite(s->i)) {    /* drop a non-finite current outright (NaN/Inf on the wire) */
        meas.current_rms[ch] = s->i;
        meas.has_current[ch] = true;
        any = true;
        if (s->has_v && isfinite(s->p)) {  /* real power only with a finite voltage reference */
            meas.power_active[ch] = s->p;   /* signed: + import / - export */
            meas.has_power[ch]    = true;
            meas.direction[ch]    = (s->p >  0.05f) ? ACROUTER_DIR_CONSUMING
                                  : (s->p < -0.05f) ? ACROUTER_DIR_SUPPLYING
                                                    : ACROUTER_DIR_ZERO;
        } else {
            meas.direction[ch] = ACROUTER_DIR_UNKNOWN;
        }
    }
    if ((role == ESPNOW_ROLE_GRID || role == ESPNOW_ROLE_VOLTAGE) && s->has_v && s->v > 1.0f) {
        meas.voltage_rms = s->v;
        meas.has_voltage = true;
        any = true;
    }

    meas.valid = any;
    if (any) {
        esp_event_post(ACROUTER_EVENT, ACROUTER_EVENT_POWER_UPDATE, &meas, sizeof(meas), 0);
    }
}

/* ---- output-node helpers ---- */

/* find/alloc an output-node slot for a MAC — call under s_mux. */
static out_node_t *out_slot(const uint8_t mac[6])
{
    for (int i = 0; i < ESP_NOW_SOURCE_OUT_NODES_MAX; i++)
        if (s_out[i].used && mac_eq(s_out[i].mac, mac)) return &s_out[i];
    for (int i = 0; i < ESP_NOW_SOURCE_OUT_NODES_MAX; i++)
        if (!s_out[i].used) { s_out[i].used = true; memcpy(s_out[i].mac, mac, 6); return &s_out[i]; }
    /* Table full — evict the oldest node past the offline timeout (D11). A driven
     * output node is refreshed by keep-alive ACKs, so only a genuinely gone node ages
     * out; if all are live we drop this HELLO rather than evict an active output. */
    const int64_t now = esp_timer_get_time();
    int oldest = -1; int64_t oldest_us = 0;
    for (int i = 0; i < ESP_NOW_SOURCE_OUT_NODES_MAX; i++) {
        if ((now - s_out[i].last_frame_us) <= (int64_t)ESPNOW_OUT_OFFLINE_MS * 1000) continue;
        if (oldest < 0 || s_out[i].last_frame_us < oldest_us) { oldest = i; oldest_us = s_out[i].last_frame_us; }
    }
    if (oldest < 0) return NULL;
    memset(&s_out[oldest], 0, sizeof(s_out[oldest]));
    s_out[oldest].used = true;
    memcpy(s_out[oldest].mac, mac, 6);
    return &s_out[oldest];
}

/* find an existing output-node slot (no alloc) — call under s_mux. */
static out_node_t *out_find(const uint8_t mac[6])
{
    for (int i = 0; i < ESP_NOW_SOURCE_OUT_NODES_MAX; i++)
        if (s_out[i].used && mac_eq(s_out[i].mac, mac)) return &s_out[i];
    return NULL;
}

static wifi_interface_t out_ifidx(void)
{
    wifi_mode_t m = WIFI_MODE_NULL;
    esp_wifi_get_mode(&m);
    return (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA) ? WIFI_IF_AP : WIFI_IF_STA;
}

/* ensure `mac` is a registered ESP-NOW peer (OPEN bring-up). NOT under s_mux. */
static esp_err_t out_ensure_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) return ESP_OK;
    esp_now_peer_info_t p = {0};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;                 /* use the current WiFi channel */
    p.ifidx   = out_ifidx();
    p.encrypt = false;             /* OPEN until the encrypted-pairing phase */
    esp_err_t err = esp_now_add_peer(&p);
    if (err != ESP_OK) ESP_LOGW(TAG, "add_peer failed: %s", esp_err_to_name(err));
    return err;
}

/* Build + send a SET_OUTPUT for one output. NOT under s_mux (does esp_now_send). */
static esp_err_t out_send(const uint8_t mac[6], uint8_t output_id, uint8_t kind,
                          uint16_t value, uint16_t ramp_ms)
{
    esp_err_t perr = out_ensure_peer(mac);
    if (perr != ESP_OK) return perr;

    rbn_set_output_t f;
    memset(&f, 0, sizeof(f));
    /* Atomic RMW — out_send() runs from both the inject task (keep-alive) and the
     * control/web/serial task; a plain s_out_seq++ could lose an increment (D7). */
    uint32_t seq = __atomic_fetch_add(&s_out_seq, 1, __ATOMIC_RELAXED);
    rbn_hdr_init(&f.h, RBN_MSG_SET_OUTPUT, seq, 0);
    f.output_id = output_id;
    f.kind      = kind;
    f.value     = value;
    f.ramp_ms   = ramp_ms;
    f.flags     = 0;
    return esp_now_send(mac, (const uint8_t *)&f, sizeof(f));
}

/* ---- ESP-NOW receive callback (runs in the WiFi task — keep it minimal) ---- */

/* HELLO → register/refresh an output node + its capability descriptors (under mux). */
static void on_hello(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(rbn_hello_t)) return;
    const rbn_hello_t *m = (const rbn_hello_t *)data;
    if (!(m->flags & RBN_HELLO_F_HAS_OUTPUTS) || m->out_count == 0) return;  /* output nodes only */
    uint8_t oc = m->out_count;
    if (oc > ESP_NOW_SOURCE_OUT_PER_NODE) oc = ESP_NOW_SOURCE_OUT_PER_NODE;
    if (len < (int)(sizeof(rbn_hello_t) + (size_t)oc * sizeof(rbn_out_cap_t))) return;
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_mux);
    out_node_t *n = out_slot(info->src_addr);
    if (n) {
        n->family        = m->family;
        n->hw_model      = m->hw_model;
        n->out_count     = oc;
        n->last_frame_us = now;
        for (uint8_t i = 0; i < oc; i++) n->caps[i] = m->out_cap[i];
    }
    portEXIT_CRITICAL(&s_mux);
}

/* OUTPUT_STATE → record applied value + ack + liveness (under mux). */
static void on_output_state(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(rbn_output_state_t)) return;
    const rbn_output_state_t *m = (const rbn_output_state_t *)data;
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_mux);
    out_node_t *n = out_find(info->src_addr);
    if (n) {
        n->last_frame_us = now;
        n->last_ack_seq  = m->ack_seq;
        n->failsafe      = (m->flags & RBN_OUTSTATE_F_FAILSAFE_ACTIVE) != 0;
        for (uint8_t i = 0; i < n->out_count && i < ESP_NOW_SOURCE_OUT_PER_NODE; i++) {
            if (n->caps[i].output_id == m->output_id) {
                n->applied_val[i] = m->value;
                n->last_result[i] = m->result;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len < (int)sizeof(rbn_hdr_t)) return;
    const rbn_hdr_t *h = (const rbn_hdr_t *)data;
    if (!rbn_hdr_ok(h)) return;

    if (h->msg_type == RBN_MSG_HELLO)        { on_hello(info, data, len);        return; }
    if (h->msg_type == RBN_MSG_OUTPUT_STATE) { on_output_state(info, data, len); return; }
    if (h->msg_type != RBN_MSG_REALTIME) return;   /* sensor path below */
    if (len < (int)(sizeof(rbn_realtime_t) + sizeof(rbn_rt_rec_t))) return;

    const rbn_realtime_t *m = (const rbn_realtime_t *)data;
    if (m->rec_count < 1) return;
    const rbn_rt_rec_t *rec = &m->recs[0];         /* primary channel */
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_mux);
    seen_t *s = seen_slot(info->src_addr);
    if (s) {
        s->i       = rec->i_rms;
        s->v       = rec->v_rms;
        s->p       = rec->p_active;
        s->pf      = rec->pf_x1000 / 1000.0f;
        s->freq    = rec->freq_x100 / 100.0f;
        s->has_v   = (rec->v_rms > 0.5f);
        s->last_us = now;
        s->fresh   = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

/* ---- keep-alive: re-assert every driven output at <= FAILSAFE_MS/2 (inject task) ---- */
static void out_keepalive_tick(void)
{
    const int64_t now = esp_timer_get_time();
    for (int i = 0; i < ESP_NOW_SOURCE_OUT_NODES_MAX; i++) {
        uint8_t  mac[6];
        bool     due = false;
        uint8_t  ids[ESP_NOW_SOURCE_OUT_PER_NODE], kinds[ESP_NOW_SOURCE_OUT_PER_NODE];
        uint16_t vals[ESP_NOW_SOURCE_OUT_PER_NODE], ramps[ESP_NOW_SOURCE_OUT_PER_NODE];
        uint8_t  ndrive = 0;

        portENTER_CRITICAL(&s_mux);
        if (s_out[i].used &&
            (now - s_out[i].last_cmd_us) >= (int64_t)ESPNOW_OUT_KEEPALIVE_MS * 1000) {
            for (uint8_t k = 0; k < s_out[i].out_count && k < ESP_NOW_SOURCE_OUT_PER_NODE; k++) {
                if (!s_out[i].desired_set[k]) continue;
                ids[ndrive]   = s_out[i].caps[k].output_id;
                kinds[ndrive] = s_out[i].caps[k].kind;
                vals[ndrive]  = s_out[i].desired_val[k];
                ramps[ndrive] = 0;   /* re-assert is a hold, no re-ramp */
                ndrive++;
            }
            if (ndrive) { due = true; memcpy(mac, s_out[i].mac, 6); s_out[i].last_cmd_us = now; }
        }
        portEXIT_CRITICAL(&s_mux);

        if (!due) continue;
        for (uint8_t d = 0; d < ndrive; d++) out_send(mac, ids[d], kinds[d], vals[d], ramps[d]);
    }
}

/* ---- inject task: drain fresh samples off-callback, post to Sensor Hub ---- */
static void esp_now_inject_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Inject task started (interval=%dms)", ESPNOW_INJECT_MS);
    while (s_running) {
        for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++) {
            portENTER_CRITICAL(&s_mux);
            bool go = s_seen[i].used && s_seen[i].fresh;
            seen_t snap = s_seen[i];
            s_seen[i].fresh = false;
            portEXIT_CRITICAL(&s_mux);
            if (!go) continue;
            post_node(&snap, role_for_mac(snap.mac), (uint8_t)i);
        }
        out_keepalive_tick();   /* re-assert driven outputs so nodes hold off failsafe */
        vTaskDelay(pdMS_TO_TICKS(ESPNOW_INJECT_MS));
    }
    ESP_LOGI(TAG, "Inject task stopped");
    s_inject_task = NULL;
    vTaskDelete(NULL);
}

/* ---- public API ---- */

esp_err_t esp_now_source_init(void)
{
    if (s_initialized) return ESP_OK;

#if CONFIG_ACROUTER_ESPNOW_CHANNEL > 0
    esp_wifi_set_channel(CONFIG_ACROUTER_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
#endif
    uint8_t prim = 0; wifi_second_chan_t sec;
    esp_wifi_get_channel(&prim, &sec);

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init: %s", esp_err_to_name(err));
        return err;
    }
    esp_now_register_recv_cb(on_recv);

    esp_now_source_load_config();
    s_initialized = true;
    ESP_LOGI(TAG, "Initialized (open RX) on WiFi channel %u", prim);
    return ESP_OK;
}

esp_err_t esp_now_source_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_inject_task != NULL) return ESP_OK;
    s_running = true;
    /* ESP-NOW TX shares the WiFi radio → keep it on the comms core (PRO_CPU/core 0,
     * with WiFi) on dual-core (ESP32); no affinity on single-core (C2). The control
     * loop never blocks on this — it only writes desired levels (see tiering plan §11.3). */
#if !CONFIG_FREERTOS_UNICORE
    const BaseType_t espnow_core = 0;
#else
    const BaseType_t espnow_core = tskNO_AFFINITY;
#endif
    if (xTaskCreatePinnedToCore(esp_now_inject_task, "espnow_inject", 4096, NULL, 4,
                                &s_inject_task, espnow_core) != pdPASS) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void esp_now_source_stop(void)
{
    s_running = false;
    for (int i = 0; i < 50 && s_inject_task != NULL; i++) vTaskDelay(pdMS_TO_TICKS(20));
    if (s_initialized) {
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        s_initialized = false;
    }
}

esp_err_t esp_now_source_set_role(const uint8_t mac[6], esp_now_source_role_t role)
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    esp_err_t rc = ESP_ERR_NO_MEM;
    /* Mutate the commissioning table under s_mux so role_for_mac() readers never see a
     * torn mac/role or a half-populated slot (D3). Pure memory ops — no blocking. */
    portENTER_CRITICAL(&s_mux);
    /* update / remove existing */
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++) {
        if (s_node_role[i].used && mac_eq(s_node_role[i].mac, mac)) {
            if (role == ESPNOW_ROLE_NONE) s_node_role[i].used = false;
            else                          s_node_role[i].role = (uint8_t)role;
            rc = ESP_OK;
            goto done;
        }
    }
    if (role == ESPNOW_ROLE_NONE) { rc = ESP_OK; goto done; }
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++) {
        if (!s_node_role[i].used) {
            memcpy(s_node_role[i].mac, mac, 6);
            s_node_role[i].role = (uint8_t)role;
            s_node_role[i].used = true;   /* publish the slot last */
            rc = ESP_OK;
            goto done;
        }
    }
done:
    portEXIT_CRITICAL(&s_mux);
    return rc;
}

esp_err_t esp_now_source_get_nodes(esp_now_source_node_info_t *out, size_t max, size_t *n)
{
    if (!out || !n) return ESP_ERR_INVALID_ARG;
    const int64_t now = esp_timer_get_time();
    size_t cnt = 0;
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES && cnt < max; i++) {
        portENTER_CRITICAL(&s_mux);
        seen_t s = s_seen[i];
        portEXIT_CRITICAL(&s_mux);
        if (!s.used) continue;
        memcpy(out[cnt].mac, s.mac, 6);
        out[cnt].role         = role_for_mac(s.mac);
        out[cnt].online       = (s.last_us != 0) && ((now - s.last_us) < (int64_t)ESPNOW_PRESENCE_TO_MS * 1000);
        out[cnt].voltage      = s.has_v ? s.v : 0.0f;
        out[cnt].current      = s.i;
        out[cnt].power        = s.has_v ? s.p : 0.0f;
        out[cnt].power_factor = s.pf;
        out[cnt].frequency    = s.freq;
        cnt++;
    }
    *n = cnt;
    return ESP_OK;
}

esp_err_t esp_now_source_save_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ESPNOW_SRC_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    uint8_t count = 0;
    char key[8];
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++) {
        if (!s_node_role[i].used) continue;
        snprintf(key, sizeof(key), "m%u", (unsigned)count);
        nvs_set_blob(nvs, key, s_node_role[i].mac, 6);
        snprintf(key, sizeof(key), "r%u", (unsigned)count);
        nvs_set_u8(nvs, key, s_node_role[i].role);
        count++;
    }
    nvs_set_u8(nvs, "n", count);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Saved %u node role(s) to NVS", (unsigned)count);
    return err;
}

esp_err_t esp_now_source_load_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ESPNOW_SRC_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    uint8_t count = 0;
    if (nvs_get_u8(nvs, "n", &count) != ESP_OK) { nvs_close(nvs); return ESP_ERR_NVS_NOT_FOUND; }
    if (count > ESP_NOW_SOURCE_MAX_NODES) count = ESP_NOW_SOURCE_MAX_NODES;
    memset(s_node_role, 0, sizeof(s_node_role));
    char key[8];
    for (uint8_t i = 0; i < count; i++) {
        snprintf(key, sizeof(key), "m%u", (unsigned)i);
        size_t len = 6;
        if (nvs_get_blob(nvs, key, s_node_role[i].mac, &len) != ESP_OK || len != 6) continue;
        snprintf(key, sizeof(key), "r%u", (unsigned)i);
        uint8_t r = 0;
        nvs_get_u8(nvs, key, &r);
        s_node_role[i].used = true;
        s_node_role[i].role = r;
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded %u node role(s) from NVS", (unsigned)count);
    return ESP_OK;
}

size_t esp_now_source_seen_count(void)
{
    size_t c = 0;
    for (int i = 0; i < ESP_NOW_SOURCE_MAX_NODES; i++)
        if (s_seen[i].used) c++;
    return c;
}

/* ---- output-node public API ---- */

esp_err_t esp_now_source_set_output(const uint8_t mac[6], uint8_t output_id,
                                    uint8_t kind, uint16_t value, uint16_t ramp_ms)
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    const int64_t now = esp_timer_get_time();
    bool found = false;

    portENTER_CRITICAL(&s_mux);
    out_node_t *n = out_find(mac);
    if (n) {
        for (uint8_t k = 0; k < n->out_count && k < ESP_NOW_SOURCE_OUT_PER_NODE; k++) {
            if (n->caps[k].output_id == output_id) {
                n->desired_set[k]  = true;
                n->desired_val[k]  = value;
                n->desired_ramp[k] = ramp_ms;
                n->last_cmd_us     = now;
                found = true;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&s_mux);

    if (!found) return ESP_ERR_NOT_FOUND;   /* unknown node/output (not yet HELLO'd) */
    return out_send(mac, output_id, kind, value, ramp_ms);   /* send now; keep-alive re-asserts */
}

esp_err_t esp_now_source_get_output_nodes(esp_now_source_output_node_info_t *out,
                                          size_t max, size_t *n)
{
    if (!out || !n) return ESP_ERR_INVALID_ARG;
    const int64_t now = esp_timer_get_time();
    size_t cnt = 0;
    for (int i = 0; i < ESP_NOW_SOURCE_OUT_NODES_MAX && cnt < max; i++) {
        portENTER_CRITICAL(&s_mux);
        out_node_t s = s_out[i];
        portEXIT_CRITICAL(&s_mux);
        if (!s.used) continue;
        esp_now_source_output_node_info_t *o = &out[cnt];
        memset(o, 0, sizeof(*o));
        memcpy(o->mac, s.mac, 6);
        o->family    = s.family;
        o->hw_model  = s.hw_model;
        o->out_count = s.out_count;
        o->online    = (s.last_frame_us != 0) &&
                       ((now - s.last_frame_us) < (int64_t)ESPNOW_OUT_OFFLINE_MS * 1000);
        o->failsafe  = s.failsafe;
        for (uint8_t k = 0; k < s.out_count && k < ESP_NOW_SOURCE_OUT_PER_NODE; k++) {
            o->outputs[k].output_id   = s.caps[k].output_id;
            o->outputs[k].kind        = s.caps[k].kind;
            o->outputs[k].range_min   = s.caps[k].range_min;
            o->outputs[k].range_max   = s.caps[k].range_max;
            o->outputs[k].desired     = s.desired_val[k];
            o->outputs[k].desired_set = s.desired_set[k];
            o->outputs[k].applied     = s.applied_val[k];
            o->outputs[k].result      = s.last_result[k];
        }
        cnt++;
    }
    *n = cnt;
    return ESP_OK;
}
