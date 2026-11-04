#pragma once

#include "daisy.h"
#include "ff.h"
#include "WavParser.h"
#include "FileReader.h"

namespace daisy
{
/**
 * WAV file Streaming Playback
 *
 * At this time, this class only supports streaming of  16-bit WAV Files
 * The output of this class will be in float converted from 16-bit integers
 * and linearly interpolated for non-integer playback speeds.
 *
 * Due to the implementation, reverse playback is not possible with this class.
 *
 * The workspace_bytes template parameter is used to set the size in bytes of
 * audio samples within the FIFO.
 *
 * The bulk of amount of memory used by this class is approximately:
 * (2 * workspace_bytes);
 * This could hypothetically be reduced by half by directly accessing the
 * FIFO's inner array (requires modifications to FIFO class), or making
 * a new type of queue data structure
 *
 * Whenever the Stream function results in a the samples FIFO being less than 75%
 * full, it will generate a request for new data. So the average disk i/o
 * transaction will be the `workspace_bytes` / 4. However,
 * There are times, like when restarting playback, or opening a different file,
 * that will trigger the entire buffer to be filled.
 */
template <size_t workspace_bytes>
class WavPlayer
{
  public:
    WavPlayer() {}
    ~WavPlayer() {}

    /** Return values for status, and errors. */
    enum class Result
    {
        Ok,
        FileNotFoundError,
        PlaybackUnderrun,
        PrepareOverrun,
        NewSamplesRequested,
        DiskError,
        InitialLatency,
    };

    /** Relevent audio file data for playback details */
    struct FileInfo
    {
        size_t channels, length, samplerate, data_start;
        size_t data_size_bytes;
    };

    /** Initialize, and open a single file by name for playback */
    Result Init(const char* name)
    {
        /** Open the file */
        auto res = Open(name);
        if(res != Result::Ok)
            return res;

        for(size_t i = 0; i < kMaxAudioChannels; i++)
        {
            current_sample_[i]  = 0.f;
            previous_sample_[i] = 0.f;
        }
        pos_acc_        = 0.f;
        playback_speed_ = 1.f;
        looping_        = false;
        playing_        = false;

        return Result::Ok;
    }

    /** Open a file, and prepare audio for streaming */
    Result Open(const char* name)
    {
        if(is_open_)
            f_close(&file_);
        auto sta = f_open(&file_, name, (FA_OPEN_EXISTING | FA_READ));
        switch(sta)
        {
            case FR_OK: break;
            case FR_NO_FILE:
            case FR_NO_PATH: return Result::FileNotFoundError;
            default: return Result::DiskError;
        }
        is_open_ = true;

        daisy::FileReader reader(&file_);
        daisy::WavParser  parser;
        if(!parser.parse(reader))
            return Result::DiskError;
        const auto& info           = parser.info();
        file_info_.channels        = info.numChannels;
        file_info_.samplerate      = info.sampleRate;
        auto bd                    = info.bitsPerSample;
        file_info_.data_size_bytes = parser.dataSize();
        file_info_.length
            = file_info_.data_size_bytes / ((bd / 8) * file_info_.channels);
        file_info_.data_start = parser.dataOffset();

        // Compute frame size (bytes per sample frame)
        frame_bytes_ = (file_info_.channels) * (bd / 8);
        if(frame_bytes_ == 0)
            return Result::DiskError;

        // Seek to start of data
        if(f_lseek(&file_, file_info_.data_start) != FR_OK)
            return Result::DiskError;

        // Prime FIFO with frame-aligned read
        std::fill(buff_raw_, buff_raw_ + kRxSizeSamples, 0);
        UINT   bytes_read = 0;
        size_t bytes_to_read
            = std::min((size_t)workspace_bytes, file_info_.data_size_bytes);
        // Align down to full frames
        bytes_to_read -= (bytes_to_read % frame_bytes_);
        if(bytes_to_read > 0)
        {
            if(f_read(&file_, (void*)buff_raw_, bytes_to_read, &bytes_read)
               != FR_OK)
            {
                f_close(&file_);
                return Result::DiskError;
            }
            if(bytes_read != bytes_to_read)
            {
                f_close(&file_);
                return Result::DiskError;
            }
        }

        buff_fifo_.Clear();
        size_t samps_to_write = bytes_read / sizeof(int16_t);
        // Align push count to whole frames (multiples of channels)
        if(file_info_.channels > 0)
            samps_to_write -= (samps_to_write % file_info_.channels);
        for(size_t i = 0; i < samps_to_write; i++)
        {
            if(!buff_fifo_.PushBack(buff_raw_[i]))
                break;
        }

        position_            = 0;
        pending_read_req_    = false;
        awaiting_first_data_ = false;
        pending_seek_req_    = false;
        handled_gen_         = restart_gen_;
        bytes_left_in_chunk_ = (bytes_read <= file_info_.data_size_bytes)
                                   ? (file_info_.data_size_bytes - bytes_read)
                                   : 0;

        return Result::Ok;
    }


    /** Close a file, and clear the data */
    Result Close()
    {
        f_close(&file_);
        file_info_.channels   = 0;
        file_info_.data_start = 0;
        file_info_.length     = 0;
        file_info_.samplerate = 0;
        is_open_              = false;
        playing_              = false;
        return Result::Ok;
    }

    /** To be executed in the main while loop, or other interruptable areas of code.
     *  This will perform the actual Disk I/O for streaming audio into the buffers
     *  used for playback.
     */
    Result Prepare()
    {
        for(;;)
        {
            // Capture the restart generation, used to discard unwanted data
            // if a restart occurs in the callback during Disk I/O.
            const uint32_t gen = restart_gen_;
            if(gen != handled_gen_)
            {
                handled_gen_ = gen;
                request_fifo_.Clear();
                if(f_lseek(&file_, file_info_.data_start) != FR_OK)
                {
                    Close();
                    return Result::DiskError;
                }
                bytes_left_in_chunk_ = file_info_.data_size_bytes;
                // Smaller first read gets audio started sooner (less underrun)
                size_t first = kRxSizeSamples / 4;
                auto   res   = ReadIntoFifo(first, gen);
                if(res != Result::Ok)
                {
                    if(res == Result::DiskError)
                        Close();
                    return res;
                }
                continue;
            }
            if(request_fifo_.IsEmpty())
                break;
            auto req = request_fifo_.PopFront();
            if(req.type != IoRequest::Type::Read)
                continue;
            auto res = ReadIntoFifo(req.data, gen);
            if(res != Result::Ok)
            {
                if(res == Result::DiskError)
                    Close();
                return res;
            }
        }
        return Result::Ok;
    }

    /** Stream Audio from disk at the current playback speed.
     *
     *    Each call to this will increment the playback position's
     *  internal accumulator by the playback speed.
     *    Anytime this accumulator exceeds 1.0, it will update it's
     *  position tracker, and pop the next sample from the FIFO of
     *  audio samples.
     *    Whenever the contents of the audio sample FIFO fall below
     *  75% of it's capacity, a request is generated to refill it.
     *    The maximum playback speed possible is limited to the following
     *  factors:
     *  - SD Card Bus-width
     *  - SD Card Clock Speed
     *  - workspace_bytes setting (consequently, transfer sizes)
     *
     *  It is possible to allow higher playback speeds, and improve
     *  bandwidth by using higher workspace sizes, with the trade-offs
     *  being memory, and latency with certain transactions.
     *
     *  @param samples buffer of floats to fill with audio samples from disk
     *
     *  @param num_channels number of channels provided to fill. This can be
     *  different from the number of channels in the file.
     */
    Result Stream(float* samples, size_t num_channels)
    {
        auto channels = file_info_.channels;

        for(size_t i = 0; i < num_channels; i++)
            samples[i] = 0.f;

        // immediately return if no file is open.
        // could probably use a different result name that's more specific
        if(!is_open_)
            return Result::FileNotFoundError;

        if(!buff_fifo_.IsEmpty() && playing_)
        {
            awaiting_first_data_ = false;
            size_t ch_out        = std::min(channels, num_channels);
            for(size_t i = 0; i < ch_out; i++)
            {
                samples[i]
                    = previous_sample_[i]
                      + pos_acc_ * (current_sample_[i] - previous_sample_[i]);
            }

            pos_acc_ += playback_speed_;
            while(pos_acc_ >= 1.f)
            {
                position_ += 1;
                pos_acc_ -= 1.f;
                for(size_t i = 0; i < channels; i++)
                {
                    previous_sample_[i] = current_sample_[i];
                    current_sample_[i]  = s162f(buff_fifo_.PopFront());
                }
            }
        }

        if(position_ >= (file_info_.length > 0 ? file_info_.length : 1))
        {
            position_ = 0;
            if(!looping_)
            {
                playing_ = false;
            }
            else
            {
                pos_acc_ = 0.f;
                for(size_t i = 0; i < kMaxAudioChannels; i++)
                    previous_sample_[i] = current_sample_[i];
            }
        }

        // Request new samples in whole frames
        bool requested_new_samps = false;
        if(buff_fifo_.GetNumElements() < kRxFifoThreshold && !pending_read_req_)
        {
            size_t free_slots = (kRxSizeSamples - buff_fifo_.GetNumElements());
            size_t rx_qty     = (free_slots > 1) ? (free_slots - 1) : 0;
            // Align to multiples of channels
            if(file_info_.channels > 0)
                rx_qty -= (rx_qty % file_info_.channels);

            if(rx_qty > 0)
            {
                request_fifo_.PushBack(
                    IoRequest(IoRequest::Type::Read, rx_qty));
                pending_read_req_   = true;
                requested_new_samps = true;
            }
        }
        if(requested_new_samps)
            return Result::NewSamplesRequested;
        else if(buff_fifo_.IsEmpty() && playing_)
            return awaiting_first_data_ ? Result::InitialLatency
                                        : Result::PlaybackUnderrun;
        else
            return Result::Ok;
    }

    /** Clear all playback samples, and return to the beginning of the audio file immediately */
    void Restart()
    {
        pos_acc_ = 0.f;
        for(size_t i = 0; i < kMaxAudioChannels; i++)
            current_sample_[i] = previous_sample_[i] = 0.f;
        if(!is_open_)
            return;
        pending_read_req_    = true;
        awaiting_first_data_ = true;
        position_            = 0;
        playing_             = true;
        buff_fifo_.Clear();
        // Restart generation increments will flag the `Prepare()` method to
        // discard any stale data that is in progress.
        restart_gen_ = restart_gen_ + 1;
    }


    /** Return the number of samples in the open audio file */
    inline size_t GetDurationInSamples() const
    {
        return file_info_.length > 0 ? file_info_.length : 1;
    }

    /** Return the number of audio channels in the open audio file */
    inline size_t GetChannels() const { return file_info_.channels; }

    /** Returns the position of the playhead in samples from the start of the file */
    inline uint32_t GetPosition() const { return position_; }

    /** Returns a 0-1 representation of the playhead position within the file. */
    inline float GetNormalizedPosition() const
    {
        size_t duration = GetDurationInSamples();
        return static_cast<float>(position_) / static_cast<float>(duration);
    }

    /** Set whether the audio file will automatically continue playing
     *  from the beginning after reaching the end of file.
     */
    inline void SetLooping(bool state) { looping_ = state; }

    /** Return whether the player is looping or not. */
    inline bool GetLooping() const { return looping_; }

    /** Set whether the file should play or not.
     *  The state automatically resets to false at the end of playback unless
     *  looping is also set to true.
     *
     *  The input is ignored if a file is not open already.
     */
    inline void SetPlaying(bool state)
    {
        if(!is_open_)
            return;
        playing_ = state;
    }

    inline bool GetPlaying() const { return playing_; }

    /** Direct setter of playback speed as a ratio
     *  compared to original speed.
     *  For example, 1.0 equals original speed, 0.5 is half-speed, etc.
     */
    inline void SetPlaybackSpeedRatio(const float speed)
    {
        if(speed >= 0.f)
        {
            playback_speed_ = speed;
        }
    }

    /** Sets playback speed as a number of semitones offset from original pitch
     *  For example, +7 a ratio of 1.5, +12 a ratio of 2, -12 a ratio of 0.5, etc.
     */
    inline void SetPlaybackSpeedSemitones(const float semitones)
    {
        playback_speed_ = std::pow(2.f, semitones / 12.f);
    }

  private:
    /** Reads (frame-aligned) samples from disk and pushes them into the FIFO.
     *  If a Restart() occurred since `gen` was captured, the data is discarded.
     */
    Result ReadIntoFifo(size_t samples, uint32_t gen)
    {
        size_t bytes_requested = samples * sizeof(int16_t);
        // Align to full frames
        bytes_requested -= (bytes_requested % frame_bytes_);
        if(bytes_requested == 0)
        {
            if(gen == restart_gen_)
                pending_read_req_ = false;
            return Result::Ok;
        }

        UINT total_bytes_read = 0;
        UINT bytes_read       = 0;

        std::fill(buff_raw_, buff_raw_ + kRxSizeSamples, 0);

        // Read up to end of data chunk
        size_t first_span = std::min(bytes_requested, bytes_left_in_chunk_);
        // Align first_span to frame boundary as well
        first_span -= (first_span % frame_bytes_);
        if(first_span > 0)
        {
            if(f_read(&file_, (void*)buff_raw_, first_span, &bytes_read)
               != FR_OK)
                return Result::DiskError;
            if(bytes_read != first_span)
                return Result::DiskError;
            total_bytes_read += bytes_read;
            bytes_left_in_chunk_ -= bytes_read;
        }

        // If need more and looping, wrap and read more (frame-aligned)
        if(total_bytes_read < bytes_requested && looping_)
        {
            if(f_lseek(&file_, file_info_.data_start) != FR_OK)
                return Result::DiskError;

            size_t remaining_bytes = bytes_requested - total_bytes_read;
            // Align remaining as well (it already is, but keep consistent)
            remaining_bytes -= (remaining_bytes % frame_bytes_);
            if(remaining_bytes > 0)
            {
                UINT  bytes_read2 = 0;
                char* tbuff       = ((char*)(buff_raw_) + total_bytes_read);
                // Do not exceed the chunk size on wrap
                size_t span2
                    = std::min(remaining_bytes, file_info_.data_size_bytes);
                // Align span2
                span2 -= (span2 % frame_bytes_);
                if(span2 > 0)
                {
                    if(f_read(&file_, (void*)tbuff, span2, &bytes_read2)
                       != FR_OK)
                        return Result::DiskError;
                    if(bytes_read2 != span2)
                        return Result::DiskError;
                    total_bytes_read += bytes_read2;
                    bytes_left_in_chunk_
                        = (bytes_read2 <= file_info_.data_size_bytes)
                              ? (file_info_.data_size_bytes - bytes_read2)
                              : 0;
                }
            }
        }

        // Count total samples to write to FIFO
        size_t samps_to_write = total_bytes_read / sizeof(int16_t);
        if(file_info_.channels > 0)
            samps_to_write -= (samps_to_write % file_info_.channels);

        // Push samples to FIFO in chunks, disabling IRQs during each
        // chunk. A Restart (from the audio callback) will cause the
        // remaining data to be discarded to avoid unwanted, stale data.
        constexpr size_t kPushChunk = 128;
        size_t           i          = 0;
        while(i < samps_to_write)
        {
            size_t   end     = std::min(i + kPushChunk, samps_to_write);
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            if(gen != restart_gen_)
            {
                __set_PRIMASK(primask);
                return Result::Ok; // stale: discard
            }
            bool full = false;
            for(; i < end; i++)
            {
                if(!buff_fifo_.PushBack(buff_raw_[i]))
                {
                    full = true;
                    break;
                }
            }
            __set_PRIMASK(primask);
            if(full)
            {
                pending_read_req_ = false;
                return Result::PrepareOverrun;
            }
        }
        pending_read_req_ = false;
        return Result::Ok;
    }

    /** Request containing necessary data to seek, or refill the
     * streaming FIFO of samples
     */
    struct IoRequest
    {
        enum class Type
        {
            Read,
            Seek,
            Unknown,
        };
        /** type of request to be made */
        Type type;

        /** multi-purpose value for size/position depending on type of request.
         *  For read requests this will be the qty of samples to request.
         *  For seek requests this will be the position in samples to jump to.
         */
        size_t data;

        /** Constructor for IoRequest: */
        IoRequest(Type t, size_t val)
        {
            type = t;
            data = val;
        }

        IoRequest() : type(Type::Unknown), data(0) {}

        ~IoRequest() {}
    };

    /** Number of samples that fit into the workspace */
    static const constexpr size_t kRxSizeSamples
        = (workspace_bytes / sizeof(int16_t));

    /** Threshold at which new samples are requested to fill up the FIFO */
    static const constexpr size_t kRxFifoThreshold = ((kRxSizeSamples / 4) * 3);

    /** Currently maximum support is for stereo files.. */
    static const constexpr size_t kMaxAudioChannels = 8;

    /** Queues and Buffers */

    /** Primary Request Queue for Disk I/O */
    daisy::FIFO<IoRequest, 8> request_fifo_;

    /** Buffer containing the samples requested from disk. */
    daisy::FIFO<int16_t, kRxSizeSamples> buff_fifo_;

    /** Intermediate buffer between the Disk I/O and the FIFO
     *  With modifications to the existing FIFO class, or a specialized
     *  SampleQueue of some sort, we could remove the requirement
     *  for this intermediate buffer.
     */
    alignas(32) int16_t buff_raw_[kRxSizeSamples];

    /** File Specific Information  */

    size_t   position_;  //< position within audio data (in samples)
    FileInfo file_info_; //< Info for the currently open file

    /** Playback Parameters */
    bool  looping_, playing_;
    float playback_speed_;

    /** Converted float sample used for interpolated varispeed playback */
    float current_sample_[kMaxAudioChannels];

    /** Previous float sample used for interpolated varispeed playback */
    float previous_sample_[kMaxAudioChannels];

    /** Internal resources */

    alignas(32) FIL file_;
    alignas(32) bool is_open_;
    bool pending_read_req_;
    bool pending_seek_req_;
    /** Set by Restart(), cleared once the first samples are streamed.
     *  Distinguishes startup latency from true mid-playback underruns. */
    volatile bool     awaiting_first_data_ = false;
    volatile uint32_t restart_gen_ = 0; //< bumped by Restart() (audio context)
    uint32_t handled_gen_ = 0; //< last generation serviced by Prepare()
    float    pos_acc_;
    size_t   bytes_left_in_chunk_; //< remaining bytes in WAV data chunk
    size_t frame_bytes_; //< bytes per sample frame (channels*bytes-per-sample)
};


} // namespace daisy
