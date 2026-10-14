// Host fixture for the actual ItlIwx timer/release methods, extracted in CI.
// It replaces only kernel clocks, timers and packet delivery, not those methods.
#include "../itlwm/hal_iwx/RxTiming.hpp"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <vector>

static uint64_t nowUsec;
struct CTimeout {
    bool pending = false, allocated = false;
    uint64_t due = 0; unsigned arms = 0;
    void (*callback)(void *) = nullptr; void *argument = nullptr;
};
static int timeout_add_msec(CTimeout **timer, int delay) {
    assert(delay > 0); (*timer)->pending = true;
    (*timer)->due = nowUsec + uint64_t(delay) * 1000; ++(*timer)->arms; return 1;
}
static int timeout_add_usec(CTimeout **timer, int delay) {
    return timeout_add_msec(timer, delay / 1000);
}
static int timeout_pending(CTimeout **timer) { return (*timer)->pending; }
static int timeout_del(CTimeout **timer) { (*timer)->pending = false; return 1; }
static int timeout_initialized(CTimeout **timer) { return *timer != nullptr; }
static void timeout_set(CTimeout **timer, void (*callback)(void *), void *argument) {
    if (!*timer) { *timer = new CTimeout; (*timer)->allocated = true; }
    (*timer)->pending = false; (*timer)->callback = callback; (*timer)->argument = argument;
}
static int timeout_free(CTimeout **timer) {
    if (*timer && (*timer)->allocated) delete *timer;
    *timer = nullptr; return 1;
}
static void getmicrouptime(timeval *time) {
    time->tv_sec = nowUsec / 1000000; time->tv_usec = nowUsec % 1000000;
}
static void USEC_TO_TIMEVAL(uint64_t usec, timeval *time) {
    time->tv_sec = usec / 1000000; time->tv_usec = usec % 1000000;
}
static int splnet() { return 0; }
static void splx(int) {}
#define XYLog(...) ((void)0)
#define container_of(pointer, type, member) reinterpret_cast<type *>( \
    reinterpret_cast<char *>(pointer) - offsetof(type, member))
#define SEQ_LT(a, b) (int32_t((unsigned(a) - unsigned(b)) & 0xfff) > 2048)
enum { IEEE80211_S_RUN = 4, IWX_FLAG_SHUTDOWN = 1,
       IWX_RX_REORDER_DATA_INVALID_BAID = 255, IEEE80211_REASON_TIMEOUT = 39,
       IEEE80211_RXI_SAME_SEQ = 1, IEEE80211_RXI_HWDEC_SAME_PN = 2 };
#define RX_REORDER_BUF_TIMEOUT_MQ_USEC (100000ULL)
struct Packet { unsigned id; Packet *next = nullptr; };
using mbuf_t = Packet *;
struct mbuf_list { mbuf_t head = nullptr; };
#define MBUF_LIST_INITIALIZER() {}
static bool ml_empty(const mbuf_list *list) { return !list->head; }
static mbuf_t ml_dequeue(mbuf_list *list) {
    auto packet = list->head;
    if (packet) { list->head = packet->next; packet->next = nullptr; }
    return packet;
}
static void ml_purge(mbuf_list *list) { while (ml_dequeue(list)) {} }
struct ieee80211_node {};
struct ieee80211_rxinfo { unsigned rxi_flags = 0; };
struct ieee80211com {
    struct { unsigned is_ht_rx_ba_timeout = 0; unsigned is_ht_rx_ba_window_gap_timeout = 0; } ic_stats;
    ieee80211_node *ic_bss = nullptr;
    int ic_state = IEEE80211_S_RUN;
    struct {} ic_if;
};
struct iwx_softc { ieee80211com sc_ic; int sc_flags = 0; };
struct iwx_rxba_data;
struct iwx_reorder_buffer {
    uint16_t head_sn = 0, buf_size = 64;
    unsigned num_stored = 0, removed = 0;
    unsigned last_amsdu = 0, last_sub_index = 0, valid = 0;
    unsigned consec_oldsn_drops = 0, consec_oldsn_ampdu_gp2 = 0, consec_oldsn_prev_drop = 0;
    CTimeout *reorder_timer;
    iwx_rxba_data *owner;
};
struct iwx_reorder_buf_entry {
    mbuf_list frames; timeval reorder_time = {};
    int chanidx = 0, is_shortpre = 0;
    uint32_t rx_pkt_status = 0, rate_n_flags = 0, device_timestamp = 0;
    ieee80211_rxinfo rxi;
};
struct iwx_rxba_data {
    iwx_softc *sc;
    uint8_t baid = 0, tid = 0;
    uint32_t timeout = 1000000;
    timeval last_rx = {};
    CTimeout *session_timer;
    iwx_reorder_buffer reorder_buf;
    iwx_reorder_buf_entry entries[256];
};
static iwx_rxba_data *iwx_rxba_data_from_reorder_buf(iwx_reorder_buffer *buffer) { return buffer->owner; }
static unsigned delba;
static void ieee80211_delba_request(ieee80211com *, ieee80211_node *, int, int, uint8_t) { ++delba; }
static void if_input(void *, mbuf_list *) {}
class ItlIwx {
public:
    iwx_softc com;
    static void iwx_rx_ba_session_expired(void *);
    static void iwx_reorder_timer_expired(void *);
    static int iwx_is_sn_less(uint16_t, uint16_t, uint16_t);
    void iwx_init_reorder_buffer(iwx_reorder_buffer *, uint16_t, uint16_t);
    void iwx_clear_reorder_buffer(iwx_softc *, iwx_rxba_data *);
    void iwx_release_frames(iwx_softc *, ieee80211_node *, iwx_rxba_data *, iwx_reorder_buffer *, uint16_t, mbuf_list *);
    void iwx_rx_frame(iwx_softc *, mbuf_t packet, int, uint32_t, int, uint32_t,
                      uint32_t, ieee80211_rxinfo *info, mbuf_list *) {
        delivered.push_back(packet->id); flags.push_back(info->rxi_flags);
    }
    static std::vector<unsigned> delivered, flags;
};
std::vector<unsigned> ItlIwx::delivered, ItlIwx::flags;
#include "iwx_rx_timer_functions.inc"

struct Fixture {
    ItlIwx driver;
    CTimeout gap, session;
    iwx_rxba_data ba = {};
    mbuf_list output;
    Fixture() {
        ba.sc = &driver.com; ba.session_timer = &session;
        ba.reorder_buf.reorder_timer = &gap; ba.reorder_buf.owner = &ba;
        nowUsec = 0; delba = 0;
        ItlIwx::delivered.clear(); ItlIwx::flags.clear();
    }
    ~Fixture() {
        if (ba.reorder_buf.reorder_timer && ba.reorder_buf.reorder_timer->allocated)
            timeout_free(&ba.reorder_buf.reorder_timer);
        if (ba.session_timer && ba.session_timer->allocated)
            timeout_free(&ba.session_timer);
    }
    void store(uint16_t sequence, Packet *packet, uint64_t received) {
        auto &entry = ba.entries[sequence % ba.reorder_buf.buf_size];
        entry.frames.head = packet; nowUsec = received; getmicrouptime(&entry.reorder_time);
        ++ba.reorder_buf.num_stored;
    }
    void release(uint16_t nssn) {
        driver.iwx_release_frames(&driver.com, nullptr, &ba, &ba.reorder_buf, nssn, &output);
    }
    void fireGap() { gap.pending = false; ItlIwx::iwx_reorder_timer_expired(&ba.reorder_buf); }
};

int main() {
    {
        // Exercise real clear/init across several BA stop/start cycles.
        Fixture f;
        for (unsigned restart = 0; restart < 4; ++restart) {
            f.driver.iwx_clear_reorder_buffer(&f.driver.com, &f.ba);
            assert(!f.ba.session_timer && !f.ba.reorder_buf.reorder_timer);
            f.driver.iwx_init_reorder_buffer(&f.ba.reorder_buf, 0, 64);
            assert(f.ba.session_timer && f.ba.reorder_buf.reorder_timer);
            assert(f.ba.session_timer->callback == ItlIwx::iwx_rx_ba_session_expired);
            assert(f.ba.reorder_buf.reorder_timer->callback == ItlIwx::iwx_reorder_timer_expired);
            assert(f.ba.session_timer->argument == &f.ba);
            assert(f.ba.reorder_buf.reorder_timer->argument == &f.ba.reorder_buf);
            Packet packet{1}; f.store(1, &packet, 0); f.release(0);
            auto timer = f.ba.reorder_buf.reorder_timer;
            assert(timer->pending && timer->due == 100000);
            nowUsec = timer->due; timer->pending = false; timer->callback(timer->argument);
            assert(!f.ba.reorder_buf.num_stored);
        }
        assert(ItlIwx::delivered.size() == 4);
    }
    {
        Fixture f; Packet packet{1}; f.store(1, &packet, 0); f.release(0);
        for (nowUsec = 10000; nowUsec < 100000; nowUsec += 10000) f.release(0);
        assert(f.gap.due == 100000 && f.gap.arms == 1);
        f.fireGap();
        assert(ItlIwx::delivered == std::vector<unsigned>{1});
        assert(!f.gap.pending && !f.ba.reorder_buf.num_stored);
    }
    {
        Fixture f; Packet first{1}, second{3};
        f.store(1, &first, 0); f.release(0);
        f.store(3, &second, 90000); f.release(2);
        nowUsec = 100000; f.fireGap();
        assert(f.gap.due == 190000); // remaining lifetime, not 200000
        assert(ItlIwx::delivered == std::vector<unsigned>{1});
        nowUsec = 190000; f.fireGap();
        assert((ItlIwx::delivered == std::vector<unsigned>{1, 3}));
    }
    {
        Fixture f; f.ba.reorder_buf.head_sn = 4095; f.ba.reorder_buf.buf_size = 63;
        Packet packet{0}; f.store(0, &packet, 0); f.release(4095);
        nowUsec = 100000; f.fireGap();
        assert(ItlIwx::delivered == std::vector<unsigned>{0});
    }
    {
        Fixture f; Packet second{2}, first{1, &second}; f.store(1, &first, 0);
        ++f.ba.reorder_buf.num_stored; f.release(2);
        assert((ItlIwx::delivered == std::vector<unsigned>{1, 2}));
        assert(ItlIwx::flags[0] == 0);
        assert(ItlIwx::flags[1] == (IEEE80211_RXI_SAME_SEQ | IEEE80211_RXI_HWDEC_SAME_PN));
        assert(!f.ba.reorder_buf.num_stored);
    }
    {
        Fixture f; nowUsec = 1800000; getmicrouptime(&f.ba.last_rx);
        nowUsec = 2000000; ItlIwx::iwx_rx_ba_session_expired(&f.ba);
        assert(!delba && f.session.due == 2800000);
        nowUsec = 2800000; ItlIwx::iwx_rx_ba_session_expired(&f.ba);
        assert(delba == 1);
    }
    {
        Fixture f; f.ba.timeout = 0; ItlIwx::iwx_rx_ba_session_expired(&f.ba);
        assert(!delba && !f.session.pending);
        f.ba.timeout = 10000; nowUsec = 10000;
        ItlIwx::iwx_rx_ba_session_expired(&f.ba); assert(delba == 1);
    }
    puts("actual iwx RX timer integration tests passed");
}
