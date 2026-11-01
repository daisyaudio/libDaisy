#include <gtest/gtest.h>
#include <deque>
#include <vector>
#include "usbh_midi.h"

namespace
{
struct Submission
{
    uint8_t* buffer;
    std::vector<uint8_t> bytes;
};
std::vector<Submission> submissions;
std::deque<USBH_URBStateTypeDef> states;
USBH_URBStateTypeDef defaultState;
USBH_StatusTypeDef sendResult;
uint32_t ticks;
int clearCount;
int closeCount;

class UsbMidiTransmitTest : public ::testing::Test
{
  protected:
    USBH_HandleTypeDef host{};
    MIDI_HandleTypeDef* midi;
    uint8_t first[4] = {9, 0x90, 60, 127};
    uint8_t next[4] = {8, 0x80, 60, 0};

    void SetUp() override
    {
        submissions.clear();
        states.clear();
        defaultState = USBH_URB_IDLE;
        sendResult = USBH_OK;
        ticks = 0;
        clearCount = closeCount = 0;
        host.pActiveClass = &USBH_midi;
        auto& interface = host.device.CfgDesc.Itf_Desc[0];
        interface.bNumEndpoints = 2;
        interface.Ep_Desc[0].bEndpointAddress = 0x81;
        interface.Ep_Desc[0].wMaxPacketSize = 64;
        interface.Ep_Desc[1].bEndpointAddress = 2;
        interface.Ep_Desc[1].wMaxPacketSize = 64;
        ASSERT_EQ(USBH_midi.Init(&host), USBH_OK);
        midi = static_cast<MIDI_HandleTypeDef*>(USBH_midi.pData);
    }

    void TearDown() override { USBH_midi.DeInit(&host); }
};
}

extern "C"
{
uint32_t HAL_GetTick(void) { return ticks++; }
uint8_t USBH_FindInterface(USBH_HandleTypeDef*, uint8_t, uint8_t, uint8_t) { return 0; }
USBH_StatusTypeDef USBH_SelectInterface(USBH_HandleTypeDef*, uint8_t) { return USBH_OK; }
uint8_t USBH_AllocPipe(USBH_HandleTypeDef*, uint8_t ep) { return ep & 0x7f; }
USBH_StatusTypeDef USBH_OpenPipe(USBH_HandleTypeDef*, uint8_t, uint8_t,
                                uint8_t, uint8_t, uint8_t, uint16_t) { return USBH_OK; }
USBH_StatusTypeDef USBH_ClosePipe(USBH_HandleTypeDef*, uint8_t) { ++closeCount; return USBH_OK; }
USBH_StatusTypeDef USBH_FreePipe(USBH_HandleTypeDef*, uint8_t) { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef*, uint8_t, uint8_t) { return USBH_OK; }
USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef*, uint8_t)
{
    if(states.empty())
        return defaultState;
    auto state = states.front();
    states.pop_front();
    return state;
}
uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef*, uint8_t) { return 0; }
USBH_StatusTypeDef USBH_ClrFeature(USBH_HandleTypeDef*, uint8_t) { ++clearCount; return USBH_OK; }
USBH_StatusTypeDef USBH_BulkReceiveData(USBH_HandleTypeDef*, uint8_t*, uint16_t, uint8_t) { return USBH_OK; }
USBH_StatusTypeDef USBH_BulkSendData(USBH_HandleTypeDef*, uint8_t* buffer,
                                    uint16_t length, uint8_t, uint8_t)
{
    submissions.push_back({buffer, std::vector<uint8_t>(buffer, buffer + length)});
    return sendResult;
}
}

TEST_F(UsbMidiTransmitTest, CopiesIntoOwnedBuffer)
{
    ASSERT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_OK);
    ASSERT_EQ(submissions.size(), 1u);
    EXPECT_EQ(submissions[0].buffer, midi->txBuffer);
    EXPECT_NE(submissions[0].buffer, first);
    first[2] = 1;
    EXPECT_EQ(midi->txBuffer[2], 60);
    EXPECT_TRUE(midi->txActive);
}

TEST_F(UsbMidiTransmitTest, RepeatedTimeoutsPreservePendingBuffer)
{
    ASSERT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_OK);
    for(int attempt = 0; attempt < 2; ++attempt)
    {
        EXPECT_EQ(USBH_MIDI_Transmit(&host, next, sizeof(next)), MIDI_BUSY);
        EXPECT_TRUE(midi->txActive);
        EXPECT_EQ(submissions.size(), 1u);
        EXPECT_EQ(midi->txBuffer[0], first[0]);
    }
    defaultState = USBH_URB_DONE;
    EXPECT_EQ(USBH_MIDI_Transmit(&host, next, sizeof(next)), MIDI_OK);
    EXPECT_EQ(submissions.size(), 2u);
    EXPECT_EQ(midi->txBuffer[0], next[0]);
}

TEST_F(UsbMidiTransmitTest, DisconnectResetsPendingTransfer)
{
    ASSERT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_OK);
    ASSERT_EQ(USBH_midi.DeInit(&host), USBH_OK);
    EXPECT_EQ(closeCount, 2);
    EXPECT_FALSE(midi->txActive);
    EXPECT_EQ(midi->txSize, 0);
    EXPECT_EQ(USBH_midi.pData, nullptr);
    EXPECT_EQ(USBH_MIDI_Transmit(&host, next, sizeof(next)), MIDI_ERROR);
    ASSERT_EQ(USBH_midi.Init(&host), USBH_OK);
    EXPECT_EQ(USBH_MIDI_Transmit(&host, next, sizeof(next)), MIDI_OK);
    EXPECT_EQ(submissions.size(), 2u);
}

TEST_F(UsbMidiTransmitTest, EmptySendDoesNotCreateOrClearPendingTransfer)
{
    EXPECT_EQ(USBH_MIDI_Transmit(&host, nullptr, 0), MIDI_OK);
    EXPECT_FALSE(midi->txActive);
    ASSERT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_OK);
    EXPECT_EQ(USBH_MIDI_Transmit(&host, nullptr, 0), MIDI_OK);
    EXPECT_TRUE(midi->txActive);
    EXPECT_EQ(submissions.size(), 1u);
}

TEST_F(UsbMidiTransmitTest, PartialTimeoutIsNotRetryableAndKeepsBuffer)
{
    uint8_t data[128] = {};
    data[0] = 9;
    data[64] = 8;
    EXPECT_EQ(USBH_MIDI_Transmit(&host, data, sizeof(data)), MIDI_ERROR);
    ASSERT_EQ(submissions.size(), 1u);
    EXPECT_TRUE(midi->txActive);
    EXPECT_EQ(USBH_MIDI_Transmit(&host, next, sizeof(next)), MIDI_BUSY);
    EXPECT_EQ(submissions.size(), 1u);
    EXPECT_EQ(midi->txBuffer[0], 9);
}

TEST_F(UsbMidiTransmitTest, ChunksAreBoundedAndKeepByteOrder)
{
    midi->OutEpSize = 512;
    uint8_t data[132];
    for(size_t i = 0; i < sizeof(data); ++i)
        data[i] = static_cast<uint8_t>(i);
    states = {USBH_URB_DONE, USBH_URB_DONE};
    ASSERT_EQ(USBH_MIDI_Transmit(&host, data, sizeof(data)), MIDI_OK);
    ASSERT_EQ(submissions.size(), 3u);
    std::vector<uint8_t> received;
    for(const auto& submission : submissions)
    {
        EXPECT_LE(submission.bytes.size(), static_cast<size_t>(USBH_MIDI_TX_BUF_SIZE));
        received.insert(received.end(), submission.bytes.begin(), submission.bytes.end());
    }
    EXPECT_EQ(received, std::vector<uint8_t>(data, data + sizeof(data)));
    EXPECT_TRUE(midi->txActive);
}

TEST_F(UsbMidiTransmitTest, NakRetriesOldPacketBeforeNewPacket)
{
    ASSERT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_OK);
    states = {USBH_URB_NOTREADY, USBH_URB_IDLE, USBH_URB_DONE};
    ASSERT_EQ(USBH_MIDI_Transmit(&host, next, sizeof(next)), MIDI_OK);
    ASSERT_EQ(submissions.size(), 3u);
    EXPECT_EQ(submissions[0].bytes, submissions[1].bytes);
    EXPECT_EQ(submissions[2].bytes, std::vector<uint8_t>(next, next + sizeof(next)));
}

TEST_F(UsbMidiTransmitTest, RepeatedNaksTimeOutWithoutReplacingPacket)
{
    ASSERT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_OK);
    defaultState = USBH_URB_NOTREADY;
    EXPECT_EQ(USBH_MIDI_Transmit(&host, next, sizeof(next)), MIDI_BUSY);
    EXPECT_TRUE(midi->txActive);
    EXPECT_LT(ticks, 100u);
    for(const auto& submission : submissions)
        EXPECT_EQ(submission.bytes, std::vector<uint8_t>(first, first + sizeof(first)));
}

TEST_F(UsbMidiTransmitTest, TerminalErrorsReleaseBufferAndReportFailure)
{
    for(auto state : {USBH_URB_ERROR, USBH_URB_STALL})
    {
        ASSERT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_OK);
        defaultState = state;
        EXPECT_EQ(USBH_MIDI_Transmit(&host, next, sizeof(next)), MIDI_ERROR);
        EXPECT_FALSE(midi->txActive);
    }
    EXPECT_EQ(clearCount, 2);
}

TEST_F(UsbMidiTransmitTest, FailedSubmissionDoesNotCreatePendingTransfer)
{
    sendResult = USBH_FAIL;
    EXPECT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_ERROR);
    EXPECT_FALSE(midi->txActive);
}

TEST_F(UsbMidiTransmitTest, InvalidHostOrEndpointDoesNotSubmit)
{
    EXPECT_EQ(USBH_MIDI_Transmit(nullptr, first, sizeof(first)), MIDI_ERROR);
    EXPECT_EQ(USBH_MIDI_Transmit(&host, nullptr, sizeof(first)), MIDI_ERROR);
    midi->OutEpSize = 0;
    EXPECT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_ERROR);
    host.pActiveClass = nullptr;
    EXPECT_EQ(USBH_MIDI_Transmit(&host, first, sizeof(first)), MIDI_ERROR);
    host.pActiveClass = &USBH_midi;
    EXPECT_TRUE(submissions.empty());
}
