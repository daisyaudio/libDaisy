/**
 * @author sauloverissimo
 * @brief USB MIDI 2.0 host stress validator (pairs with MIDI2_USB_Device)
 * @date 2026-06-10
 *
 * USB MIDI 2.0 Host: stress validator for the paired MIDI2_USB_Device emitter
 *
 * Daisy Seed enumerates the device plugged into its USB-A jack as a
 * USB MIDI 2.0 host, selects Alt 1 (UMP), and validates the incoming
 * stream against the emitter contract:
 *
 *     per burst the device emits 4001 UMP packets:
 *         2000 NoteOn  (Group 0 Ch 0, note = 48 + i%24,
 *                       velocity high-16 = seq, seq runs 0..1999)
 *         2000 NoteOff (same note, velocity 0)
 *         1    Marker  (CC 119 Group 0 Ch 0, value = burst_id)
 *     400 ms quiet between bursts.
 *
 * Reporting model: every received UMP word is counted in cumulative
 * monotonic counters inside the RX callback. The main loop reads the
 * counters and prints a one-line HEARTBEAT every second, plus a final
 * RESULT line when the stream goes idle (emitter unplugged or paused).
 * The host usually attaches mid-burst, so the verdict measures the
 * span BETWEEN the first and last marker. Pass criteria:
 *
 *     received between markers == (last_id - first_id) * 4001
 *     gaps                     == 0
 *     unexpected               == 0
 *
 * Counters are 32-bit monotonics incremented in the RX callback, so no
 * data can ever be lost to a reporting race; the main loop only reads.
 */
#include "daisy_seed.h"
#include "hid/usb_host.h"
#include "hid/usb_midi.h"
#include "usbh_midi.h"

using namespace daisy;

/** Global hardware access */
DaisySeed        hw;
USBHostHandle    usbHost;
MidiUsbTransport midi;

/** UMP constants (M2-104-UM, matches the MIDI2_USB_Device emitter) */
static constexpr uint32_t kMT4Mask          = 0xF0000000U;
static constexpr uint32_t kMT4Marker        = 0x40000000U;
static constexpr uint32_t kStatusMask       = 0x00F00000U;
static constexpr uint32_t kStatusNoteOn     = 0x00900000U;
static constexpr uint32_t kStatusNoteOff    = 0x00800000U;
static constexpr uint32_t kStatusCC         = 0x00B00000U;
static constexpr uint8_t  kMarkerCC         = 119U;
static constexpr uint16_t kExpectedPerBurst = 4001U;
static constexpr uint32_t kHeartbeatMs      = 1000U;
static constexpr uint32_t kIdleMs           = 1500U;

/** Cumulative counters. Every increment happens in the RX callback;
 *  the main loop only reads. Volatile because read/write cross the
 *  callback / main boundary; on Cortex-M7 32-bit aligned word loads
 *  are atomic so torn reads of individual fields are not a concern.
 */
volatile uint32_t g_total_messages    = 0;
volatile uint32_t g_note_on           = 0;
volatile uint32_t g_note_off          = 0;
volatile uint32_t g_marker_count      = 0;
volatile uint32_t g_unexpected        = 0;
volatile uint32_t g_gaps              = 0;
volatile uint32_t g_first_burst_id    = 0;
volatile uint32_t g_last_burst_id     = 0;
volatile bool     g_first_marker_seen = false;
volatile uint32_t g_last_rx_ms        = 0;

/** Totals latched at marker boundaries. The host usually attaches in
 *  the middle of a burst, so the verdict measures the span BETWEEN the
 *  first and last marker: that window must hold an exact multiple of
 *  the burst size.
 */
volatile uint32_t g_total_at_first_marker = 0;
volatile uint32_t g_total_at_last_marker  = 0;

/** Sequence tracking for NoteOn contiguity. Lives across the whole
 *  run; the emitter's seq rolls 0..1999 within each burst so a delta
 *  of +1 is expected inside a burst and a roll-back to 0 is expected
 *  at the burst boundary (right after the marker).
 */
static uint16_t g_last_seq       = 0;
static bool     g_last_seq_valid = false;

static inline void HandleWord(uint32_t w0, uint32_t w1)
{
    g_total_messages++;
    if((w0 & kMT4Mask) != kMT4Marker)
    {
        g_unexpected++;
        return;
    }
    const uint32_t status = w0 & kStatusMask;
    if(status == kStatusNoteOn)
    {
        g_note_on++;
        const uint16_t seq = static_cast<uint16_t>(w1 >> 16);
        if(g_last_seq_valid)
        {
            const uint16_t expected = g_last_seq + 1U;
            if(seq != expected)
                g_gaps++;
        }
        g_last_seq       = seq;
        g_last_seq_valid = true;
    }
    else if(status == kStatusNoteOff)
    {
        g_note_off++;
    }
    else if(status == kStatusCC && ((w0 >> 8) & 0xFFU) == kMarkerCC)
    {
        g_marker_count++;
        const uint32_t burst_id = w1 & 0xFFFFU;
        if(!g_first_marker_seen)
        {
            g_first_burst_id        = burst_id;
            g_first_marker_seen     = true;
            g_total_at_first_marker = g_total_messages;
        }
        g_last_burst_id        = burst_id;
        g_total_at_last_marker = g_total_messages;
        /** Sequence resets at the burst boundary; arm for next burst. */
        g_last_seq_valid = false;
    }
    else
    {
        g_unexpected++;
    }
}

static void UmpRxCb(const uint32_t* words, uint8_t count, void* /*ctx*/)
{
    g_last_rx_ms = System::GetNow();
    for(uint8_t i = 0; i + 1 < count; i += 2)
        HandleWord(words[i], words[i + 1]);
}

/** MIDI 1.0 byte path is not used by the emitter, but the transport
 *  needs a parse callback registered to activate StartRx.
 */
static void NoopParseCb(uint8_t*, size_t, void*) {}

static void USBH_Connect(void* /*data*/)
{
    hw.PrintLine("device connected");
}
static void USBH_Disconnect(void* /*data*/)
{
    hw.PrintLine("device disconnected");
}
static void USBH_Error(void* /*data*/)
{
    hw.PrintLine("USB error");
}

static void USBH_ClassActive(void* /*data*/)
{
    if(usbHost.IsActiveClass(USBH_MIDI_CLASS))
    {
        hw.PrintLine("MIDI class active");
        MidiUsbTransport::Config cfg;
        cfg.periph = MidiUsbTransport::Config::HOST;
        midi.Init(cfg);
        midi.SetUmpCallback(UmpRxCb, nullptr);
        midi.StartRx(NoopParseCb, nullptr);
    }
}

struct Snapshot
{
    uint32_t total;
    uint32_t note_on;
    uint32_t note_off;
    uint32_t marker;
    uint32_t unexpected;
    uint32_t gaps;
    uint32_t first_burst_id;
    uint32_t last_burst_id;
    uint32_t total_at_first_marker;
    uint32_t total_at_last_marker;
    bool     first_marker_seen;
};

static inline Snapshot ReadCounters()
{
    /** Single read per field. Torn reads across fields are tolerated:
     *  cumulative counters can only grow, so a heartbeat may briefly
     *  under-report by a few words while the RX callback runs between
     *  field reads; the next heartbeat corrects.
     */
    Snapshot s;
    s.total                 = g_total_messages;
    s.note_on               = g_note_on;
    s.note_off              = g_note_off;
    s.marker                = g_marker_count;
    s.unexpected            = g_unexpected;
    s.gaps                  = g_gaps;
    s.first_burst_id        = g_first_burst_id;
    s.last_burst_id         = g_last_burst_id;
    s.total_at_first_marker = g_total_at_first_marker;
    s.total_at_last_marker  = g_total_at_last_marker;
    s.first_marker_seen     = g_first_marker_seen;
    return s;
}

/** Bursts COMPLETED between the first and last marker (the partial
 *  burst received before the first marker is excluded by design).
 */
static inline uint32_t ExpectedFromMarkers(const Snapshot& s)
{
    if(!s.first_marker_seen)
        return 0;
    const uint32_t bursts = (s.last_burst_id - s.first_burst_id) & 0xFFFFU;
    return bursts * kExpectedPerBurst;
}

static void PrintLineTagged(const char* tag, const Snapshot& s)
{
    char           buf[200];
    const uint32_t expected = ExpectedFromMarkers(s);
    const uint32_t received = s.total_at_last_marker - s.total_at_first_marker;
    const bool     counters_clean
        = (s.total > 0 && s.gaps == 0 && s.unexpected == 0);
    /** Marker mode: the paired emitter brackets bursts with CC 119, so
     *  the span between the first and last marker must hold an exact
     *  multiple of the burst size. Without markers (any emitter that
     *  embeds a monotonic sequence in NoteOn velocity), the verdict
     *  falls back to the running gap / unexpected counters.
     */
    const bool  marker_exact = (s.marker == 0) || (received == expected);
    const char* verdict      = (counters_clean && marker_exact) ? "OK" : "FAIL";
    /** Compact field names: the logger truncates lines at 128 chars
     *  and the verdict must stay visible on long runs.
     */
    sprintf(buf,
            "%s bursts=%lu span=%lu..%lu recv=%lu exp=%lu "
            "on=%lu off=%lu gaps=%lu unexp=%lu => %s",
            tag,
            static_cast<unsigned long>(s.marker),
            static_cast<unsigned long>(s.first_burst_id),
            static_cast<unsigned long>(s.last_burst_id),
            static_cast<unsigned long>(received),
            static_cast<unsigned long>(expected),
            static_cast<unsigned long>(s.note_on),
            static_cast<unsigned long>(s.note_off),
            static_cast<unsigned long>(s.gaps),
            static_cast<unsigned long>(s.unexpected),
            verdict);
    hw.PrintLine(buf);
}

int main(void)
{
    hw.Init();
    /** StartLog(true) blocks until the PC opens the USB CDC virtual COM
     *  port. Without this wait, the firmware can buffer log lines before
     *  the cdc_acm driver subscribes, which blocks the main loop on the
     *  first flush and stalls usbHost.Process() during early enumeration,
     *  causing the RX path to miss URBs.
     */
    hw.StartLog(true);
    hw.PrintLine("MIDI2_Host start");

    USBHostHandle::Config usbhConfig;
    usbhConfig.connect_callback      = USBH_Connect;
    usbhConfig.disconnect_callback   = USBH_Disconnect;
    usbhConfig.class_active_callback = USBH_ClassActive;
    usbhConfig.error_callback        = USBH_Error;
    usbHost.Init(usbhConfig);
    usbHost.RegisterClass(USBH_MIDI_CLASS);

    hw.PrintLine("host ready, waiting for emitter on USB-A");

    uint32_t blink_time    = 0;
    bool     led_state     = false;
    uint32_t next_hb_ms    = kHeartbeatMs;
    bool     idle_reported = true;

    while(1)
    {
        usbHost.Process();

        const uint32_t now = System::GetNow();
        if(now >= blink_time)
        {
            hw.SetLed(led_state);
            led_state  = !led_state;
            blink_time = now + (usbHost.GetPresent() ? 400U : 80U);
        }

        /** Heartbeat: print running tally every second while data is
         *  flowing. The cumulative counters can only grow, so this is
         *  a strictly-monotonic progress line.
         */
        if(now >= next_hb_ms)
        {
            const Snapshot s = ReadCounters();
            /** Print whenever any UMP word has been received. The
             *  paired emitter's verdict logic kicks in once markers
             *  start arriving; against arbitrary MIDI 2.0 devices
             *  (no marker contract), the line still shows the running
             *  tally so the operator can verify the host is alive
             *  and the stack is draining URBs.
             */
            if(s.total > 0)
                PrintLineTagged("HEARTBEAT", s);
            next_hb_ms = now + kHeartbeatMs;
        }

        /** Idle detection: print one final RESULT line when the stream
         *  stops (emitter unplugged or paused). Fires once per quiet
         *  window; rearms automatically when data resumes.
         */
        if(g_last_rx_ms != 0U && (now - g_last_rx_ms) > kIdleMs)
        {
            if(!idle_reported)
            {
                const Snapshot s = ReadCounters();
                PrintLineTagged("RESULT", s);
                idle_reported = true;
            }
        }
        else
        {
            idle_reported = false;
        }
    }
}
