#pragma once
#ifndef DSY_MIDI_PARSER_H
#define DSY_MIDI_PARSER_H

#include <stdint.h>
#include <stdlib.h>
#include "hid/MidiEvent.h"

namespace daisy
{
/** @brief   Utility class for parsing raw byte streams into MIDI messages
 *  @details Implemented as a state machine designed to parse one byte at a time
 *  @ingroup midi
 */
class MidiParser
{
  public:
    MidiParser(){};
    ~MidiParser() {}

    inline void Init() { Reset(); }

    /**
     * @brief Parse one MIDI byte. If the byte completes a parsed event,
     *        its value will be assigned to the dereferenced output pointer.
     *        Otherwise, status is preserved in anticipation of the next sequential
     *        byte. Return value indicates if a new event was parsed or not.
     *
     * @param byte      Raw MIDI byte to parse
     * @param event_out Pointer to output event object, value assigned on parse success
     * @return true     If a new event was parsed
     * @return false    If no new event was parsed
     */
    bool Parse(uint8_t byte, MidiEvent *event_out);

    /**
     * @brief Reset parser to default state
     */
    void Reset();

  private:
    enum ParserState
    {
        ParserEmpty,
        ParserHasStatus,
        ParserHasData0,
        ParserSysEx,
    };

    ParserState     pstate_;
    MidiEvent       incoming_message_;
    MidiMessageType running_status_;

    // Whether the message being parsed has a single data byte
    bool HasOneDataByte() const;

    // Masks to check for message type, and byte content
    const uint8_t kStatusByteMask     = 0x80;
    const uint8_t kMessageMask        = 0x70;
    const uint8_t kDataByteMask       = 0x7F;
    const uint8_t kChannelMask        = 0x0F;
    const uint8_t kSystemRealTimeMask = 0x07;
};

} // namespace daisy

#endif
