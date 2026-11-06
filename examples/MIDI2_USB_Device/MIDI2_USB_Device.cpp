/**
 * @author sauloverissimo
 * @brief USB MIDI 2.0 deterministic stress emitter (pairs with MIDI2_Host)
 * @date 2026-06-10
 *
 * USB MIDI 2.0 Device: deterministic UMP reference emitter
 *
 * Daisy Seed enumerates as a USB MIDI 2.0 device on Alt 1 (UMP).
 * The main loop emits a deterministic stress catalog at the bus rate
 * the host can drain:
 *
 *     per burst:
 *         2000 iterations of (NoteOn note, NoteOff note)
 *             note  = 48 + (i % 24)        (chromatic walk, notes 48..71)
 *             vel32 = (seq << 16)          (seq number embedded so the
 *                                           host can verify contiguity)
 *             seq increments per NoteOn inside the burst (0..1999)
 *         1 marker CC 119, value = burst_id (rolling 16-bit)
 *     hold 400 ms (quiet, observable as a cycle boundary)
 *     repeat
 *
 * Total per burst: 4001 UMP packets (2000 NoteOn + 2000 NoteOff + 1 CC).
 * Paired with the MIDI2_Host example, the host validator checks that
 * every marker-to-marker span holds exactly 4001 packets with zero
 * sequence gaps.
 */
#include "daisy_seed.h"
#include "hid/usb_midi.h"

using namespace daisy;

/** Global hardware access */
DaisySeed        hw;
MidiUsbTransport midi;

/** UMP constants (M2-104-UM)
 *
 *  Word 0 layout for MT 0x4 (MIDI 2.0 Channel Voice):
 *      bits 31..28  MT       (0x4)
 *      bits 27..24  Group    (0x0, the only group the group terminal
 *                             block declares)
 *      bits 23..20  Status   (0x9 NoteOn, 0x8 NoteOff, 0xB CC)
 *      bits 19..16  Channel  (0x0)
 *      bits 15..08  Note / Index
 *      bits 07..00  Reserved (0x00)
 */
static constexpr uint32_t kMT4NoteOn  = 0x40900000U;
static constexpr uint32_t kMT4NoteOff = 0x40800000U;
static constexpr uint32_t kMT4CC      = 0x40B00000U;
static constexpr uint8_t  kMarkerCC   = 119U;

static constexpr uint16_t kBurstCount    = 2000U;
static constexpr uint8_t  kBurstLowNote  = 48U;
static constexpr uint8_t  kBurstHighNote = 71U;
static constexpr uint32_t kBurstPauseMs  = 400U;

/** Receive callbacks: this example does not consume incoming UMP, but
 *  the transport requires both callbacks to be registered for StartRx
 *  to activate the receive pipe.
 */
static void NoopParseCb(uint8_t*, size_t, void*) {}
static void NoopUmpCb(const uint32_t*, uint8_t, void*) {}

/** Send one UMP packet (2 words = 8 bytes). MidiUsbTransport::Tx blocks
 *  until the bulk endpoint has accepted the data (subject to the
 *  tx_retry_count in Config; default 3), so the caller does not need
 *  to throttle.
 */
static inline void SendPacket(uint32_t w0, uint32_t w1)
{
    uint32_t words[2] = {w0, w1};
    midi.Tx(reinterpret_cast<uint8_t*>(words), sizeof(words));
}

static void EmitBurst(uint16_t burst_id)
{
    const uint8_t note_span = kBurstHighNote - kBurstLowNote + 1U;
    for(uint16_t i = 0; i < kBurstCount; ++i)
    {
        const uint8_t  note = kBurstLowNote + (i % note_span);
        const uint32_t seq  = static_cast<uint32_t>(i);
        const uint32_t w0_on
            = kMT4NoteOn | (static_cast<uint32_t>(note) << 8);
        const uint32_t w0_off
            = kMT4NoteOff | (static_cast<uint32_t>(note) << 8);
        SendPacket(w0_on, seq << 16);
        SendPacket(w0_off, 0U);
    }
    SendPacket(kMT4CC | (static_cast<uint32_t>(kMarkerCC) << 8),
               static_cast<uint32_t>(burst_id));
}

static void Hold(uint32_t ms, uint32_t& blink_time, bool& led_state)
{
    const uint32_t start = System::GetNow();
    while(System::GetNow() - start < ms)
    {
        const uint32_t now = System::GetNow();
        if(now >= blink_time)
        {
            hw.SetLed(led_state);
            led_state  = !led_state;
            blink_time = now + 100U;
        }
    }
}

int main(void)
{
    hw.Init();
    hw.StartLog(false);
    hw.PrintLine("MIDI2_USB_Device start");

    MidiUsbTransport::Config cfg;
    cfg.periph = MidiUsbTransport::Config::INTERNAL;
    midi.Init(cfg);
    midi.SetUmpCallback(NoopUmpCb, nullptr);
    midi.StartRx(NoopParseCb, nullptr);

    hw.PrintLine("transport up, emitting reference stream");

    uint32_t blink_time = 0;
    bool     led_state  = false;
    uint16_t burst_id   = 0;

    while(1)
    {
        burst_id++;
        EmitBurst(burst_id);
        Hold(kBurstPauseMs, blink_time, led_state);
    }
}
