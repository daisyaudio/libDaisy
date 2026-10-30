#include "midi_parser.h"

using namespace daisy;

bool MidiParser::Parse(uint8_t byte, MidiEvent* event_out)
{
    // Handle System Real-Time (0xF8-0xFF) immediately, regardless of
    // parser state. Per the MIDI spec, these single-byte messages can
    // appear anywhere in the stream — even between data bytes of another
    // message — and must NOT affect running status or parser state.
    if(byte >= 0xF8)
    {
        if(event_out != nullptr)
        {
            MidiEvent rt_event;
            rt_event.type    = SystemRealTime;
            rt_event.channel = 0;
            rt_event.srt_type
                = static_cast<SystemRealTimeType>(byte & kSystemRealTimeMask);
            *event_out = rt_event;
        }
        return true;
    }

    // reset parser when status byte is received
    bool did_parse = false;

    if((byte & kStatusByteMask) && pstate_ != ParserSysEx)
    {
        pstate_ = ParserEmpty;
    }
    switch(pstate_)
    {
        case ParserEmpty:
            // check byte for valid Status Byte
            if(byte & kStatusByteMask)
            {
                // Get MessageType, and Channel
                incoming_message_.channel = byte & kChannelMask;
                incoming_message_.type
                    = static_cast<MidiMessageType>((byte & kMessageMask) >> 4);

                // Validate, and move on.
                if(incoming_message_.type < MessageLast)
                {
                    pstate_ = ParserHasStatus;

                    if(incoming_message_.type == SystemCommon)
                    {
                        incoming_message_.channel = 0;
                        incoming_message_.sc_type
                            = static_cast<SystemCommonType>(byte & 0x07);
                        // System Common messages cancel running status
                        running_status_ = MessageLast;
                        //sysex
                        if(incoming_message_.sc_type == SystemExclusive)
                        {
                            pstate_                             = ParserSysEx;
                            incoming_message_.sysex_message_len = 0;
                        }
                        //short circuit
                        else if(incoming_message_.sc_type > SongSelect)
                        {
                            pstate_ = ParserEmpty;
                            if(event_out != nullptr)
                            {
                                *event_out = incoming_message_;
                            }
                            did_parse = true;
                        }
                    }
                    else // Channel Voice or Channel Mode
                    {
                        running_status_ = incoming_message_.type;
                    }
                }
                // Else we'll keep waiting for a valid incoming status byte
            }
            else if(running_status_ != MessageLast)
            {
                // Handle as running status (Channel Voice and Mode messages only)
                incoming_message_.type    = running_status_;
                incoming_message_.data[0] = byte & kDataByteMask;
                //check for single byte running status, really this only applies to channel pressure though
                if(HasOneDataByte())
                {
                    //Send the single byte update
                    pstate_ = ParserEmpty;
                    if(event_out != nullptr)
                    {
                        *event_out = incoming_message_;
                    }
                    did_parse = true;
                }
                else
                {
                    pstate_ = ParserHasData0; //we need to get the 2nd byte yet.
                }
            }
            break;
        case ParserHasStatus:
            if((byte & kStatusByteMask) == 0)
            {
                incoming_message_.data[0] = byte & kDataByteMask;
                if(HasOneDataByte())
                {
                    //these are just one data byte, so we short circuit back to start
                    pstate_ = ParserEmpty;
                    if(event_out != nullptr)
                    {
                        *event_out = incoming_message_;
                    }
                    did_parse = true;
                }
                else
                {
                    pstate_ = ParserHasData0;
                }

                //ChannelModeMessages (reserved Control Changes)
                if(incoming_message_.type == ControlChange
                   && incoming_message_.data[0] > 119)
                {
                    incoming_message_.type    = ChannelMode;
                    running_status_           = ChannelMode;
                    incoming_message_.cm_type = static_cast<ChannelModeType>(
                        incoming_message_.data[0] - 120);
                }
            }
            else
            {
                // invalid message go back to start ;p
                pstate_ = ParserEmpty;
            }
            break;
        case ParserHasData0:
            if((byte & kStatusByteMask) == 0)
            {
                incoming_message_.data[1] = byte & kDataByteMask;

                //velocity 0 NoteOns are NoteOffs
                if(running_status_ == NoteOn && incoming_message_.data[1] == 0)
                {
                    incoming_message_.type = NoteOff;
                }

                // At this point the message is valid, and we can complete this MidiEvent
                if(event_out != nullptr)
                {
                    *event_out = incoming_message_;
                }
                did_parse = true;
            }
            else
            {
                // invalid message go back to start ;p
                pstate_ = ParserEmpty;
            }
            // Regardless, of whether the data was valid or not we go back to empty
            // because either the message is queued for handling or its not.
            pstate_ = ParserEmpty;
            break;
        case ParserSysEx:
            // end of sysex
            if(byte == 0xf7)
            {
                pstate_ = ParserEmpty;
                if(event_out != nullptr)
                {
                    *event_out = incoming_message_;
                }
                did_parse = true;
            }
            else if(incoming_message_.sysex_message_len < SYSEX_BUFFER_LEN)
            {
                incoming_message_
                    .sysex_data[incoming_message_.sysex_message_len]
                    = byte;
                incoming_message_.sysex_message_len++;
            }
            break;
        default: break;
    }

    return did_parse;
}

void MidiParser::Reset()
{
    pstate_                = ParserEmpty;
    incoming_message_.type = MessageLast;
    running_status_        = MessageLast;
}

bool MidiParser::HasOneDataByte() const
{
    // Decided by the message being parsed: the System Common type of an
    // earlier message must not shorten a later Channel message
    switch(incoming_message_.type)
    {
        case ChannelPressure:
        case ProgramChange: return true;
        case SystemCommon:
            return incoming_message_.sc_type == MTCQuarterFrame
                   || incoming_message_.sc_type == SongSelect;
        default: return false;
    }
}
