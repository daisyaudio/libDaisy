#include <gtest/gtest.h>
#include "hid/midi.h"
#include "sys/system.h"

//get rid of compiler errors over unused args in stubs
#define UNUSED(x) (void)x

using namespace daisy;

class MidiTestTransport
{
  public:
    MidiTestTransport() {}
    ~MidiTestTransport() {}

    struct Config
    {
    };

    void Init(Config conf) { UNUSED(conf); }

    //stubs to make the handler happy
    void   StartRx(){};
    size_t Readable() { return 1; }
    void   FlushRx() {}
    void   Tx(uint8_t* buff, size_t size)
    {
        UNUSED(buff);
        UNUSED(size);
    }
    uint8_t Rx() { return 1; }
    bool    RxActive() { return true; }

  private:
};

class MidiTest : public ::testing::Test
{
  protected:
    void SetUp()
    {
        MidiHandler<MidiTestTransport>::Config conf;
        midi.Init(conf);
    }

    //help with parsing messages
    void Parse(uint8_t* msgs, int size)
    {
        for(int i = 0; i < size; i++)
        {
            midi.Parse(msgs[i]);
        }
    }

    //help with parsing messages
    MidiEvent ParseAndPop(uint8_t* msgs, int size)
    {
        Parse(msgs, size);
        return midi.PopEvent();
    }

    //help with parsing sysex messages
    MidiEvent ParseAndPopSysex(uint8_t* msgs, int size)
    {
        midi.Parse(0xf0); //sysex
        Parse(msgs, size);
        midi.Parse(0xf7); //end of sysex
        return midi.PopEvent();
    }

    MidiHandler<MidiTestTransport> midi;
};

// ================ Channel Voice Messages ================

TEST_F(MidiTest, noteOff)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t note = 0; note < 128; note++)
        {
            for(uint8_t vel = 0; vel < 128; vel++)
            {
                uint8_t      msgs[]   = {(uint8_t)(0x80 + chn), note, vel};
                MidiEvent    event    = ParseAndPop(msgs, 3);
                NoteOffEvent offEvent = event.AsNoteOff();

                EXPECT_EQ(event.type, MidiMessageType::NoteOff);
                EXPECT_EQ(offEvent.channel, chn);
                EXPECT_EQ(offEvent.note, note);
                EXPECT_EQ(offEvent.velocity, vel);
            }
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, noteOn)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t note = 0; note < 128; note++)
        {
            for(uint8_t vel = 0; vel < 128; vel++)
            {
                uint8_t msgs[] = {(uint8_t)(0x80 + (1 << 4) + chn), note, vel};
                MidiEvent   event   = ParseAndPop(msgs, 3);
                NoteOnEvent onEvent = event.AsNoteOn();

                //NoteOn of vel 0 is NoteOff
                if(vel == 0)
                    EXPECT_EQ(event.type, MidiMessageType::NoteOff);
                else
                    EXPECT_EQ(event.type, MidiMessageType::NoteOn);

                EXPECT_EQ(onEvent.channel, chn);
                EXPECT_EQ(onEvent.note, note);
                EXPECT_EQ(onEvent.velocity, vel);
            }
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, polyphonicKeyPressure)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t note = 0; note < 128; note++)
        {
            for(uint8_t pressure = 0; pressure < 128; pressure++)
            {
                uint8_t msgs[]
                    = {(uint8_t)(0x80 + (2 << 4) + chn), note, pressure};
                MidiEvent                  event = ParseAndPop(msgs, 3);
                PolyphonicKeyPressureEvent pkpEvent
                    = event.AsPolyphonicKeyPressure();

                EXPECT_EQ(event.type, MidiMessageType::PolyphonicKeyPressure);
                EXPECT_EQ(pkpEvent.channel, chn);
                EXPECT_EQ(pkpEvent.note, note);
                EXPECT_EQ(pkpEvent.pressure, pressure);
            }
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, controlChange)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t ctrl = 0; ctrl < 120; ctrl++)
        {
            for(uint8_t val = 0; val < 128; val++)
            {
                uint8_t msgs[] = {(uint8_t)(0x80 + (3 << 4) + chn), ctrl, val};
                MidiEvent          event     = ParseAndPop(msgs, 3);
                ControlChangeEvent ctrlEvent = event.AsControlChange();

                EXPECT_EQ(event.type, MidiMessageType::ControlChange);
                EXPECT_EQ(ctrlEvent.channel, chn);
                EXPECT_EQ(ctrlEvent.control_number, ctrl);
                EXPECT_EQ(ctrlEvent.value, val);
            }
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, programChange)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t prog = 0; prog < 128; prog++)
        {
            uint8_t   msgs[] = {(uint8_t)(0x80 + (4 << 4) + chn), prog};
            MidiEvent event  = ParseAndPop(msgs, 2);
            ProgramChangeEvent pgmEvent = event.AsProgramChange();

            EXPECT_EQ(event.type, MidiMessageType::ProgramChange);
            EXPECT_EQ(pgmEvent.channel, chn);
            EXPECT_EQ(pgmEvent.program, prog);
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, channelPressure)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t pressure = 0; pressure < 128; pressure++)
        {
            uint8_t   msgs[] = {(uint8_t)(0x80 + (5 << 4) + chn), pressure};
            MidiEvent event  = ParseAndPop(msgs, 2);
            ChannelPressureEvent chpEvent = event.AsChannelPressure();

            EXPECT_EQ(event.type, MidiMessageType::ChannelPressure);
            EXPECT_EQ(chpEvent.channel, chn);
            EXPECT_EQ(chpEvent.pressure, pressure);
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, pitchBend)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t d0 = 0; d0 < 128; d0++)
        {
            for(uint8_t d1 = 0; d1 < 128; d1++)
            {
                uint8_t   msgs[] = {(uint8_t)(0x80 + (6 << 4) + chn), d0, d1};
                MidiEvent event  = ParseAndPop(msgs, 3);
                PitchBendEvent pbEvent = event.AsPitchBend();

                EXPECT_EQ(event.type, MidiMessageType::PitchBend);
                EXPECT_EQ(pbEvent.channel, chn);
                EXPECT_EQ(pbEvent.value, ((uint16_t)d1 << 7) + (d0 - 8192));
            }
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, channelMode)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t ev = 120; ev < 128; ev++)
        {
            for(uint8_t v = 0; v < 128; v++)
            {
                uint8_t   msgs[] = {(uint8_t)(0x80 + (3 << 4) + chn), ev, v};
                MidiEvent event  = ParseAndPop(msgs, 3);
                ChannelModeEvent cmEvent = event.AsChannelMode();

                EXPECT_EQ(event.type, MidiMessageType::ChannelMode);
                EXPECT_EQ(cmEvent.channel, chn);
                EXPECT_EQ(cmEvent.event_type, (ChannelModeType)(ev - 120));
                EXPECT_EQ(cmEvent.value, v);
            }
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

// ================ Channel Mode Messages ================

TEST_F(MidiTest, allSoundOff)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        uint8_t          msg[]    = {(uint8_t)(0x80 + (3 << 4) + chn), 120, 0};
        MidiEvent        event    = ParseAndPop(msg, 3);
        AllSoundOffEvent asoEvent = event.AsAllSoundOff();

        EXPECT_EQ((uint8_t)event.type, ChannelMode);
        EXPECT_EQ((uint8_t)event.cm_type, AllSoundOff);
        EXPECT_EQ((uint8_t)asoEvent.channel, chn);
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, resetAllControllers)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t val = 0; val < 128; val++)
        {
            uint8_t   msg[] = {(uint8_t)(0x80 + (3 << 4) + chn), 121, val};
            MidiEvent event = ParseAndPop(msg, 3);
            ResetAllControllersEvent racEvent = event.AsResetAllControllers();

            EXPECT_EQ((uint8_t)event.type, ChannelMode);
            EXPECT_EQ((uint8_t)event.cm_type, ResetAllControllers);
            EXPECT_EQ((uint8_t)racEvent.channel, chn);
            EXPECT_EQ((uint8_t)racEvent.value, val);
        }
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, localControl)
{
    //Off
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        uint8_t           msg[]   = {(uint8_t)(0x80 + (3 << 4) + chn), 122, 0};
        MidiEvent         event   = ParseAndPop(msg, 3);
        LocalControlEvent lcEvent = event.AsLocalControl();

        EXPECT_EQ((uint8_t)event.type, ChannelMode);
        EXPECT_EQ((uint8_t)event.cm_type, LocalControl);
        EXPECT_TRUE((uint8_t)lcEvent.local_control_off);
        EXPECT_FALSE((uint8_t)lcEvent.local_control_on);
    }

    //On
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        uint8_t           msg[] = {(uint8_t)(0x80 + (3 << 4) + chn), 122, 127};
        MidiEvent         event = ParseAndPop(msg, 3);
        LocalControlEvent lcEvent = event.AsLocalControl();

        EXPECT_EQ((uint8_t)event.type, ChannelMode);
        EXPECT_EQ((uint8_t)event.cm_type, LocalControl);
        EXPECT_TRUE((uint8_t)lcEvent.local_control_on);
        EXPECT_FALSE((uint8_t)lcEvent.local_control_off);
    }

    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, allNotesOff)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        uint8_t          msg[] = {(uint8_t)(0x80 + (3 << 4) + chn), 123, 0};
        MidiEvent        event = ParseAndPop(msg, 3);
        AllNotesOffEvent allOnEvent = event.AsAllNotesOff();

        EXPECT_EQ((uint8_t)event.type, ChannelMode);
        EXPECT_EQ((uint8_t)event.cm_type, AllNotesOff);
        EXPECT_EQ((uint8_t)allOnEvent.channel, chn);
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, omniModeOff)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        uint8_t          msg[]    = {(uint8_t)(0x80 + (3 << 4) + chn), 124, 0};
        MidiEvent        event    = ParseAndPop(msg, 3);
        OmniModeOffEvent omoEvent = event.AsOmniModeOff();

        EXPECT_EQ((uint8_t)event.type, ChannelMode);
        EXPECT_EQ((uint8_t)event.cm_type, OmniModeOff);
        EXPECT_EQ((uint8_t)omoEvent.channel, chn);
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, omniModeOn)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        uint8_t         msg[]    = {(uint8_t)(0x80 + (3 << 4) + chn), 125, 0};
        MidiEvent       event    = ParseAndPop(msg, 3);
        OmniModeOnEvent omoEvent = event.AsOmniModeOn();

        EXPECT_EQ((uint8_t)event.type, ChannelMode);
        EXPECT_EQ((uint8_t)event.cm_type, OmniModeOn);
        EXPECT_EQ((uint8_t)omoEvent.channel, chn);
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, monoModeOn)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        for(uint8_t val = 0; val < 128; val++)
        {
            uint8_t   msg[] = {(uint8_t)(0x80 + (3 << 4) + chn), 126, val};
            MidiEvent event = ParseAndPop(msg, 3);
            MonoModeOnEvent mmoEvent = event.AsMonoModeOn();

            EXPECT_EQ((uint8_t)event.type, ChannelMode);
            EXPECT_EQ((uint8_t)event.cm_type, MonoModeOn);
            EXPECT_EQ((uint8_t)mmoEvent.channel, chn);
            EXPECT_EQ((uint8_t)mmoEvent.num_channels, val);
        }
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, polyModeOn)
{
    for(uint8_t chn = 0; chn < 16; chn++)
    {
        uint8_t         msg[]    = {(uint8_t)(0x80 + (3 << 4) + chn), 127, 0};
        MidiEvent       event    = ParseAndPop(msg, 3);
        PolyModeOnEvent pmoEvent = event.AsPolyModeOn();

        EXPECT_EQ((uint8_t)event.type, ChannelMode);
        EXPECT_EQ((uint8_t)event.cm_type, PolyModeOn);
        EXPECT_EQ((uint8_t)pmoEvent.channel, chn);
    }
    EXPECT_FALSE(midi.HasEvents());
}

// ================ System Common Messages ================

TEST_F(MidiTest, mtcQuarterFrame)
{
    for(uint8_t type = 0; type < 8; type++)
    {
        for(uint8_t val = 0; val < 16; val++)
        {
            uint8_t msg[]
                = {uint8_t((0x0f << 4) + 1), (uint8_t)(val + (type << 4))};
            MidiEvent            event   = ParseAndPop(msg, 2);
            MTCQuarterFrameEvent qfEvent = event.AsMTCQuarterFrame();

            EXPECT_EQ(event.type, SystemCommon);
            EXPECT_EQ(event.sc_type, MTCQuarterFrame);
            EXPECT_EQ(qfEvent.message_type, type);
            EXPECT_EQ(qfEvent.value, val);
        }
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, songPositionPointer)
{
    for(uint8_t low = 0; low < 128; low++)
    {
        for(uint8_t high = 0; high < 128; high++)
        {
            uint8_t   msg[] = {uint8_t((0x0f << 4) + 2), low, high};
            MidiEvent event = ParseAndPop(msg, 3);
            SongPositionPointerEvent sppEvent = event.AsSongPositionPointer();

            EXPECT_EQ(event.type, SystemCommon);
            EXPECT_EQ(event.sc_type, SongPositionPointer);
            EXPECT_EQ(sppEvent.position, ((uint16_t)high << 7) | low);
        }
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, songSelect)
{
    for(uint8_t song = 0; song < 128; song++)
    {
        uint8_t         msg[]   = {uint8_t((0x0f << 4) + 3), song};
        MidiEvent       event   = ParseAndPop(msg, 2);
        SongSelectEvent ssEvent = event.AsSongSelect();

        EXPECT_EQ(event.type, SystemCommon);
        EXPECT_EQ(event.sc_type, SongSelect);
        EXPECT_EQ(ssEvent.song, song);
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, scUndefined0)
{
    uint8_t   msg[] = {uint8_t((0x0f << 4) + 4), 0};
    MidiEvent event = ParseAndPop(msg, 2);
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, SCUndefined0);
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, scUndefined1)
{
    uint8_t   msg[] = {uint8_t((0x0f << 4) + 5), 0};
    MidiEvent event = ParseAndPop(msg, 2);
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, SCUndefined1);
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, tuneRequest)
{
    uint8_t   msg[] = {uint8_t((0x0f << 4) + 6), 0};
    MidiEvent event = ParseAndPop(msg, 2);
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, TuneRequest);
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, sysexEnd)
{
    uint8_t   msg[] = {uint8_t((0x0f << 4) + 7), 0};
    MidiEvent event = ParseAndPop(msg, 2);
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, SysExEnd);
    EXPECT_FALSE(midi.HasEvents());
}

// ================ System Real Time Messages ================

TEST_F(MidiTest, systemRealTime)
{
    for(uint8_t type = 0; type < 8; type++)
    {
        uint8_t   msg[] = {uint8_t(0xf8 + type), 0, 0};
        MidiEvent event = ParseAndPop(msg, 1);
        EXPECT_EQ((uint8_t)event.srt_type, type);
    }
}

// ================ System Exclusive Messages ================

TEST_F(MidiTest, systemExclusive)
{
    uint8_t msgs[135];
    for(int i = 0; i < 135; i++)
    {
        msgs[i] = (uint8_t)i;
    }

    // short message
    int                  size       = 6;
    MidiEvent            event      = ParseAndPopSysex(msgs, size);
    SystemExclusiveEvent sysexEvent = event.AsSystemExclusive();
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, SystemExclusive);

    EXPECT_EQ(sysexEvent.length, size);

    for(int i = 0; i < size; i++)
    {
        EXPECT_EQ(sysexEvent.data[i], msgs[i]);
    }

    EXPECT_FALSE(midi.HasEvents());

    // full length message
    size       = 128;
    event      = ParseAndPopSysex(msgs, size);
    sysexEvent = event.AsSystemExclusive();
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, SystemExclusive);

    EXPECT_EQ(sysexEvent.length, size);

    for(int i = 0; i < size; i++)
    {
        EXPECT_EQ(sysexEvent.data[i], msgs[i]);
    }

    EXPECT_FALSE(midi.HasEvents());

    //max len is 128, let's go past that
    size       = 135;
    event      = ParseAndPopSysex(msgs, size);
    sysexEvent = event.AsSystemExclusive();
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, SystemExclusive);

    //max len
    EXPECT_EQ(sysexEvent.length, 128);

    for(int i = 0; i < 128; i++)
    {
        EXPECT_EQ(sysexEvent.data[i], msgs[i]);
    }

    EXPECT_FALSE(midi.HasEvents());
}

// ================ Running Status ================

TEST_F(MidiTest, runningStatus)
{
    //NoteOn with status bit
    uint8_t     msgs[]  = {0x90, 0x10, 0x0f};
    MidiEvent   event   = ParseAndPop(msgs, 3);
    NoteOnEvent onEvent = event.AsNoteOn();
    EXPECT_EQ(event.type, NoteOn);
    EXPECT_EQ(onEvent.channel, 0);
    EXPECT_EQ(onEvent.note, 0x10);
    EXPECT_EQ(onEvent.velocity, 0x0f);

    //running status
    for(uint8_t i = 1; i < 20; i++)
    {
        msgs[0] = msgs[1]   = i;
        MidiEvent   event   = ParseAndPop(msgs, 2);
        NoteOnEvent onEvent = event.AsNoteOn();
        EXPECT_EQ(event.type, NoteOn);
        EXPECT_EQ(onEvent.channel, 0);
        EXPECT_EQ(onEvent.note, i);
        EXPECT_EQ(onEvent.velocity, i);
    }

    EXPECT_FALSE(midi.HasEvents());

    //Again, with Control Change, channel 3
    msgs[0]                    = 0xB3;
    msgs[1]                    = 0x10;
    msgs[2]                    = 0x0f;
    event                      = ParseAndPop(msgs, 3);
    ControlChangeEvent ccEvent = event.AsControlChange();
    EXPECT_EQ(event.type, ControlChange);
    EXPECT_EQ(ccEvent.channel, 3);
    EXPECT_EQ(ccEvent.control_number, 0x10);
    EXPECT_EQ(ccEvent.value, 0x0f);

    //running status
    for(uint8_t i = 0; i < 20; i++)
    {
        msgs[0] = msgs[1]          = i;
        MidiEvent          event   = ParseAndPop(msgs, 2);
        ControlChangeEvent ccEvent = event.AsControlChange();
        EXPECT_EQ(event.type, ControlChange);
        EXPECT_EQ(ccEvent.channel, 3);
        EXPECT_EQ(ccEvent.control_number, i);
        EXPECT_EQ(ccEvent.value, i);
    }


    EXPECT_FALSE(midi.HasEvents());
}

/** This tests that SystemRealTime (clock, etc.) messages
 *  don't clear the running status
*/
TEST_F(MidiTest, runningStatSysRealtime)
{
    /** Clock messages between running status notes */
    uint8_t realtime_mixed_bytes[] = {
        0x90, /**< Note On Status */
        0x24, /**< Note Number 36 */
        0x40, /**< velocity 40 */
        0xf8, /**, Timing Clock */
        0xf8, /**, Timing Clock */
        0xf8, /**, Timing Clock */
        0x24, /**< Note Number 36 */
        0x00, /**< velocity 00 */
    };
    /** parse */
    for(int i = 0; i < 8; i++)
    {
        midi.Parse(realtime_mixed_bytes[i]);
    }

    auto noteon1 = midi.PopEvent();
    EXPECT_EQ(noteon1.type, NoteOn);
    auto clk1 = midi.PopEvent();
    EXPECT_EQ(clk1.type, SystemRealTime);
    auto clk2 = midi.PopEvent();
    EXPECT_EQ(clk2.type, SystemRealTime);
    auto clk3 = midi.PopEvent();
    EXPECT_EQ(clk3.type, SystemRealTime);
    auto noteon2 = midi.PopEvent();
    EXPECT_EQ(noteon2.type, NoteOff);
}

/** System Real-Time bytes arriving between data bytes of a
 *  multi-byte message must not corrupt the message being parsed.
 *  This is the actual bug reported in issue #666 — clock bytes
 *  in the middle of a NoteOn caused the channel to shift.
 */
TEST_F(MidiTest, sysRealtimeMidMessage)
{
    // NoteOn with clock bytes between status, data0, and data1
    uint8_t bytes[] = {
        0x90, /**< Note On, channel 0 */
        0xF8, /**< Timing Clock (between status and data0) */
        0x3C, /**< Note Number 60 */
        0xF8, /**< Timing Clock (between data0 and data1) */
        0x7F, /**< Velocity 127 */
    };

    for(int i = 0; i < 5; i++)
    {
        midi.Parse(bytes[i]);
    }

    // RT messages are dispatched immediately (lowest latency for clock sync),
    // so the two clocks appear before the NoteOn that they interrupted.
    auto clk1 = midi.PopEvent();
    EXPECT_EQ(clk1.type, SystemRealTime);
    EXPECT_EQ(clk1.srt_type, TimingClock);

    auto clk2 = midi.PopEvent();
    EXPECT_EQ(clk2.type, SystemRealTime);
    EXPECT_EQ(clk2.srt_type, TimingClock);

    // The NoteOn must still parse correctly despite the interleaved clocks
    auto noteon = midi.PopEvent();
    EXPECT_EQ(noteon.type, NoteOn);
    NoteOnEvent onEvent = noteon.AsNoteOn();
    EXPECT_EQ(onEvent.channel, 0);
    EXPECT_EQ(onEvent.note, 60);
    EXPECT_EQ(onEvent.velocity, 127);

    EXPECT_FALSE(midi.HasEvents());
}

/** System Real-Time bytes inside a SysEx message must not corrupt
 *  the SysEx data and must be reported as separate events.
 */
TEST_F(MidiTest, sysRealtimeDuringSysEx)
{
    uint8_t bytes[] = {
        0xF0,       /**< SysEx Start */
        0x01, 0x02, /**< SysEx data */
        0xF8,       /**< Timing Clock inside SysEx */
        0x03,       /**< More SysEx data */
        0xF7,       /**< SysEx End */
    };

    for(int i = 0; i < 6; i++)
    {
        midi.Parse(bytes[i]);
    }

    // Clock should be first (parsed inline before SysEx completes)
    auto clk = midi.PopEvent();
    EXPECT_EQ(clk.type, SystemRealTime);
    EXPECT_EQ(clk.srt_type, TimingClock);

    // Then the SysEx with the correct 3 data bytes (clock excluded)
    auto sysex = midi.PopEvent();
    EXPECT_EQ(sysex.type, SystemCommon);
    EXPECT_EQ(sysex.sc_type, SystemExclusive);
    SystemExclusiveEvent se = sysex.AsSystemExclusive();
    EXPECT_EQ(se.length, 3);
    EXPECT_EQ(se.data[0], 0x01);
    EXPECT_EQ(se.data[1], 0x02);
    EXPECT_EQ(se.data[2], 0x03);

    EXPECT_FALSE(midi.HasEvents());
}

/** All 8 System Real-Time types should be correctly identified */
TEST_F(MidiTest, allSysRealtimeTypes)
{
    for(uint8_t type = 0; type < 8; type++)
    {
        uint8_t   byte  = 0xF8 + type;
        MidiEvent event = ParseAndPop(&byte, 1);
        EXPECT_EQ(event.type, SystemRealTime);
        EXPECT_EQ(event.srt_type, static_cast<SystemRealTimeType>(type));
        EXPECT_EQ(event.channel, 0);
    }
    EXPECT_FALSE(midi.HasEvents());
}

/** Running status must be preserved after System Real-Time bytes.
 *  A sequence of NoteOn messages using running status with clock
 *  bytes interleaved must all parse with the correct channel.
 */
TEST_F(MidiTest, runningStatusPreservedAfterRealtime)
{
    uint8_t bytes[] = {
        0x92,       /**< Note On, channel 2 */
        0x3C, 0x64, /**< Note 60, vel 100 */
        0xF8,       /**< Timing Clock */
        0x3E, 0x50, /**< Running status: Note 62, vel 80 */
        0xFA,       /**< Start */
        0x40, 0x00, /**< Running status: Note 64, vel 0 (NoteOff) */
    };

    for(int i = 0; i < 9; i++)
    {
        midi.Parse(bytes[i]);
    }

    // NoteOn ch2 note=60 vel=100
    auto e1 = midi.PopEvent();
    EXPECT_EQ(e1.type, NoteOn);
    EXPECT_EQ(e1.AsNoteOn().channel, 2);
    EXPECT_EQ(e1.AsNoteOn().note, 60);
    EXPECT_EQ(e1.AsNoteOn().velocity, 100);

    // Timing Clock
    auto e2 = midi.PopEvent();
    EXPECT_EQ(e2.type, SystemRealTime);
    EXPECT_EQ(e2.srt_type, TimingClock);

    // NoteOn ch2 note=62 vel=80 (running status)
    auto e3 = midi.PopEvent();
    EXPECT_EQ(e3.type, NoteOn);
    EXPECT_EQ(e3.AsNoteOn().channel, 2);
    EXPECT_EQ(e3.AsNoteOn().note, 62);
    EXPECT_EQ(e3.AsNoteOn().velocity, 80);

    // Start
    auto e4 = midi.PopEvent();
    EXPECT_EQ(e4.type, SystemRealTime);
    EXPECT_EQ(e4.srt_type, Start);

    // NoteOff ch2 note=64 (vel 0 → NoteOff, running status)
    auto e5 = midi.PopEvent();
    EXPECT_EQ(e5.type, NoteOff);
    EXPECT_EQ(e5.AsNoteOff().channel, 2);
    EXPECT_EQ(e5.AsNoteOff().note, 64);

    EXPECT_FALSE(midi.HasEvents());
}

// ================ Bad Data ================

TEST_F(MidiTest, badData)
{
    //multiple msgs with status bytes in a row
    for(uint8_t i = 0; i < 20; i++)
    {
        uint8_t msg = i | 0x80;
        Parse(&msg, 1);
    }
    EXPECT_FALSE(midi.HasEvents());

    //Too short NoteOn
    uint8_t msgs[] = {0x90, 0x00};
    for(uint8_t i = 0; i < 20; i++)
    {
        msgs[1] = i;
        Parse(msgs, 2);
    }
    EXPECT_FALSE(midi.HasEvents());
}

// send some status bytes without data between valid messages with running status
TEST_F(MidiTest, runningStatusAndStatusBytes)
{
    uint8_t noteon            = 0x90;
    uint8_t running_status[6] = {0x40, 100, 0x40, 0x01, 0x40, 100};
    uint8_t status_bytes[3]   = {0x90, 0x91, 0xB3};

    Parse(&noteon, 1);
    for(int i = 0; i < 3; i++)
    {
        MidiEvent   event   = ParseAndPop(&running_status[i * 2], 2);
        NoteOnEvent onEvent = event.AsNoteOn();
        EXPECT_EQ(event.type, NoteOn);
        EXPECT_EQ(onEvent.channel, 0);
        EXPECT_EQ(onEvent.note, 0x40);
        EXPECT_EQ(onEvent.velocity, running_status[i * 2 + 1]);
    }

    Parse(status_bytes, 3);
    EXPECT_FALSE(midi.HasEvents());

    Parse(&noteon, 1);
    for(int i = 0; i < 3; i++)
    {
        MidiEvent   event   = ParseAndPop(&running_status[i * 2], 2);
        NoteOnEvent onEvent = event.AsNoteOn();
        EXPECT_EQ(event.type, NoteOn);
        EXPECT_EQ(onEvent.channel, 0);
        EXPECT_EQ(onEvent.note, 0x40);
        EXPECT_EQ(onEvent.velocity, running_status[i * 2 + 1]);
    }

    EXPECT_FALSE(midi.HasEvents());
}

// send running status noteons alternating between velocity 100 and 0
TEST_F(MidiTest, mayoTest)
{
    uint8_t buff[60]
        = {145, 48,  100, 48, 0,   48, 100, 48, 0,   48, 100, 48, 0,   48, 100,
           48,  0,   145, 48, 100, 48, 0,   48, 100, 48, 0,   48, 100, 48, 0,
           48,  100, 145, 48, 0,   48, 100, 48, 0,   48, 100, 48, 0,   48, 100,
           48,  0,   145, 48, 100, 48, 0,   48, 100, 48, 0,   48, 100, 48, 0};

    Parse(buff, 60);
    for(int i = 0; i < 14; i++)
    {
        MidiEvent on  = midi.PopEvent();
        MidiEvent off = midi.PopEvent();

        EXPECT_EQ(on.type, NoteOn);
        EXPECT_EQ(off.type, NoteOff);

        NoteOnEvent  noOn  = on.AsNoteOn();
        NoteOffEvent noOff = off.AsNoteOff();

        EXPECT_EQ(noOn.channel, 1);
        EXPECT_EQ(noOff.channel, 1);
        EXPECT_EQ(noOn.note, 48);
        EXPECT_EQ(noOff.note, 48);
        EXPECT_EQ(noOn.velocity, 100);
        EXPECT_EQ(noOff.velocity, 0);
    }

    EXPECT_FALSE(midi.HasEvents());
}

// send running status messages with one data byte
TEST_F(MidiTest, singleByteRunningStatusTest)
{
    // == Channel Pressure ==
    uint8_t status = 0xD3; // channel pressure, channel 3
    Parse(&status, 1);

    for(uint8_t i = 0; i < 128; i++)
    {
        MidiEvent ev = ParseAndPop(&i, 1);

        EXPECT_EQ(ev.type, ChannelPressure);

        ChannelPressureEvent chPressure = ev.AsChannelPressure();

        EXPECT_EQ(chPressure.channel, 3);
        EXPECT_EQ(chPressure.pressure, i);
    }

    EXPECT_FALSE(midi.HasEvents());

    // == Program Change ==
    status = 0xC3; // program change, channel 3
    Parse(&status, 1);

    for(uint8_t i = 0; i < 128; i++)
    {
        MidiEvent ev = ParseAndPop(&i, 1);

        EXPECT_EQ(ev.type, ProgramChange);

        ProgramChangeEvent progChange = ev.AsProgramChange();

        EXPECT_EQ(progChange.channel, 3);
        EXPECT_EQ(progChange.program, i);
    }

    EXPECT_FALSE(midi.HasEvents());
}

// ================ System Common and Later Messages ================

// The type of an earlier System Common message must not change how a later
// Channel message is parsed: an MTC Quarter Frame or Song Select used to make
// every following Control Change end after its first data byte

TEST_F(MidiTest, controlChangeAfterSongSelect)
{
    uint8_t songSelect[] = {0xf3, 5};
    ParseAndPop(songSelect, 2);

    uint8_t msgs[] = {0xbf, 102, 1, 0xbf, 102, 2};
    Parse(msgs, 6);
    for(uint8_t val = 1; val <= 2; val++)
    {
        ASSERT_TRUE(midi.HasEvents());
        MidiEvent          event     = midi.PopEvent();
        ControlChangeEvent ctrlEvent = event.AsControlChange();
        EXPECT_EQ(event.type, MidiMessageType::ControlChange);
        EXPECT_EQ(ctrlEvent.channel, 15);
        EXPECT_EQ(ctrlEvent.control_number, 102);
        EXPECT_EQ(ctrlEvent.value, val);
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, runningStatusAfterMtcQuarterFrame)
{
    uint8_t quarterFrame[] = {0xf1, 0x10};
    ParseAndPop(quarterFrame, 2);

    // a Control Change, then a second one by running status
    uint8_t msgs[] = {0xbf, 102, 1, 102, 2};
    Parse(msgs, 5);
    for(uint8_t val = 1; val <= 2; val++)
    {
        ASSERT_TRUE(midi.HasEvents());
        MidiEvent          event     = midi.PopEvent();
        ControlChangeEvent ctrlEvent = event.AsControlChange();
        EXPECT_EQ(event.type, MidiMessageType::ControlChange);
        EXPECT_EQ(ctrlEvent.control_number, 102);
        EXPECT_EQ(ctrlEvent.value, val);
    }
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, songPositionPointerAfterProgramChange)
{
    uint8_t programChange[] = {0xc0, 5};
    ParseAndPop(programChange, 2);

    uint8_t   msg[]    = {0xf2, 0x10, 0x20};
    MidiEvent event    = ParseAndPop(msg, 3);
    SongPositionPointerEvent sppEvent = event.AsSongPositionPointer();
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, SongPositionPointer);
    EXPECT_EQ(sppEvent.position, ((uint16_t)0x20 << 7) | 0x10);
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, songPositionPointerAfterControlChange)
{
    // a data byte over 119 after a Control Change is not a Channel Mode message
    uint8_t controlChange[] = {0xb0, 7, 100};
    ParseAndPop(controlChange, 3);

    uint8_t   msg[] = {0xf2, 120, 1};
    MidiEvent event = ParseAndPop(msg, 3);
    EXPECT_EQ(event.type, SystemCommon);
    EXPECT_EQ(event.sc_type, SongPositionPointer);
    EXPECT_EQ(event.AsSongPositionPointer().position, (1 << 7) | 120);
    EXPECT_FALSE(midi.HasEvents());
}

TEST_F(MidiTest, systemCommonCancelsRunningStatus)
{
    uint8_t controlChange[] = {0xb0, 7, 100};
    ParseAndPop(controlChange, 3);

    uint8_t songSelect[] = {0xf3, 1};
    ParseAndPop(songSelect, 2);

    // data bytes with no status in force are ignored
    uint8_t data[] = {7, 101};
    Parse(data, 2);
    EXPECT_FALSE(midi.HasEvents());
}
