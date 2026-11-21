#pragma once
#pragma once
#include "fatfs.h"
#include "util/wav_format.h"

namespace daisy
{
/** Audio Recording Module
 **
 ** Record audio into a working buffer that is gradually written to a WAV file on an SD Card.
 **
 ** Recordings are made with floating point input, and will be converted to the
 ** specified bits per sample internally
 **
 ** For now only 16-bit and 32-bit (signed int) formats are supported
 ** f32 and s24 formats will be added next
 **
 ** The transfer size determines the amount of internal memory used, and can have an
 ** effect on the performance of the streaming behavior of the WavWriter.
 ** Memory use can be calculated as: (2 * transfer_size) bytes
 ** Performance optimal with sizes: 16384, 32768
 **
 ** To use:
 ** 1. Create a WavWriter<size> object (e.g. WavWriter<32768> writer)
 ** 2. Configure the settings as desired by creating a WavWriter<32768>::Config struct and setting the settings.
 ** 3. Initialize the object with the configuration struct.
 ** 4. Open a new file for writing with: writer.OpenFile("FileName.wav")
 ** 5. Write to it within your audio callback using: writer.Sample(value)
 ** 6. Fill the Wav File on the SD Card with data from your main loop by running: writer.Write()
 ** 7. When finished with the recording finalize, and close the file with: writer.SaveFile();
 **
 ** */
template <size_t transfer_size>
class WavWriter
{
  public:
    WavWriter() {}
    ~WavWriter() {}

    /** Return values for write related functions */
    enum class Result
    {
        OK,
        ERROR,
    };

    /** Configuration structure for the wave writer.
     ** */
    struct Config
    {
        float   samplerate;
        int32_t channels;
        int32_t bitspersample;
    };

    /** State of the internal Writing mechanism.
     ** When the buffer is a certain amount full one section will write its contents
     ** while the other is still being written to. This is performed circularly
     ** so that audio will be uninterrupted during writing. */
    enum class BufferState
    {
        IDLE,
        FLUSH0,
        FLUSH1,
    };

    /**  Initializes the WavFile header, and prepares the object for recording. */
    void Init(const Config &cfg)
    {
        cfg_       = cfg;
        num_samps_ = 0;
        wptr_      = 0;
        bstate_    = BufferState::IDLE;
        recording_ = false;
        memset(transfer_buff, 0, sizeof(transfer_buff));
        // Prep the wav header according to config.
        // Certain things (i.e. Size, etc. will have to wait until the finalization of the file, or be updated while streaming).
        wavheader_.ChunkId       = kWavFileChunkId;     /** "RIFF" */
        wavheader_.FileFormat    = kWavFileWaveId;      /** "WAVE" */
        wavheader_.SubChunk1ID   = kWavFileSubChunk1Id; /** "fmt " */
        wavheader_.SubChunk1Size = 16;                  // for PCM
        wavheader_.AudioFormat   = WAVE_FORMAT_PCM;
        wavheader_.NbrChannels   = cfg.channels;
        wavheader_.SampleRate    = static_cast<int>(cfg.samplerate);
        wavheader_.ByteRate      = CalcByteRate();
        wavheader_.BlockAlign    = cfg_.channels * cfg_.bitspersample / 8;
        wavheader_.BitPerSample  = cfg_.bitspersample;
        wavheader_.SubChunk2ID   = kWavFileSubChunk2Id; /** "data" */
        /** Also calcs SubChunk2Size */
        wavheader_.FileSize = CalcFileSize();
        // This is calculated as part of the subchunk size
    }

    /** Records the current sample into the working buffer,
     ** queues writes to media when necessary.
     **
     ** \param in should be a pointer to an array of samples */
    void Sample(const float *in)
    {
        for(int i = 0; i < cfg_.channels; i++)
        {
            switch(cfg_.bitspersample)
            {
                case 16:
                {
                    int16_t *tp;
                    tp            = (int16_t *)transfer_buff;
                    tp[wptr_ + i] = f2s16(in[i]);
                }
                break;
                case 32: transfer_buff[wptr_ + i] = f2s32(in[i]); break;
                default: break;
            }
        }
        num_samps_++;
        wptr_ += cfg_.channels;
        size_t cap_point
            = cfg_.bitspersample == 16 ? kTransferSamps * 2 : kTransferSamps;
        if(wptr_ == cap_point)
        {
            bstate_ = BufferState::FLUSH0;
        }
        if(wptr_ >= cap_point * 2)
        {
            wptr_   = 0;
            bstate_ = BufferState::FLUSH1;
        }
    }

    /** Check buffer state and write */
    void Write()
    {
        if(bstate_ != BufferState::IDLE && IsRecording())
        {
            uint32_t     offset;
            unsigned int bw = 0;
            //offset          = bstate_ == BufferState::FLUSH0 ? 0 : transfer_size;
            offset  = bstate_ == BufferState::FLUSH0 ? 0 : kTransferSamps;
            bstate_ = BufferState::IDLE;
            f_write(&fp_, &transfer_buff[offset], transfer_size, &bw);
        }
    }

    /** Finalizes the writing of the WAV file.
     ** This overwrites the WAV Header with the correct
     ** final size, and closes the fptr. */
    void SaveFile()
    {
        unsigned int bw = 0;

        // 1. Flush any pending full half-buffer first
        Write();

        // 2. Flush remaining partial data (if any)
        if(wptr_ > 0)
        {
            uint32_t bytes_per_sample = cfg_.bitspersample / 8;
            uint32_t cap_point = cfg_.bitspersample == 16 ? kTransferSamps * 2
                                                          : kTransferSamps;

            uint32_t offset_samples = 0;
            uint32_t local_wptr     = wptr_;

            // If we've crossed cap_point, the partial data sits in the 2nd half
            if(local_wptr >= cap_point)
            {
                offset_samples = cap_point;
                local_wptr -= cap_point;
            }

            uint32_t offset_bytes   = offset_samples * bytes_per_sample;
            uint32_t remaining_size = local_wptr * bytes_per_sample;

            // Clamp for safety
            uint32_t max_bytes = sizeof(transfer_buff) - offset_bytes;
            if(remaining_size > max_bytes)
                remaining_size = max_bytes;

            if(remaining_size > 0)
            {
                uint8_t *base = reinterpret_cast<uint8_t *>(transfer_buff);
                f_write(&fp_, base + offset_bytes, remaining_size, &bw);
            }
        }

        // 3. Finalize header and close
        recording_          = false;
        wavheader_.FileSize = CalcFileSize();
        f_lseek(&fp_, 0);
        f_write(&fp_, &wavheader_, sizeof(wavheader_), &bw);
        f_close(&fp_);

        // 4. Reset internal state
        memset(transfer_buff, 0, sizeof(transfer_buff));
        bstate_    = BufferState::IDLE;
        wptr_      = 0;
        num_samps_ = 0;
        recording_ = false;
    }

    /** Opens a file for writing. Writes the initial WAV Header, and gets ready for stream-based recording. */
    void OpenFile(const char *name)
    {
        if(f_open(&fp_, name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
        {
            unsigned int bw = 0;
            if(f_write(&fp_, &wavheader_, sizeof(wavheader_), &bw) == FR_OK)
            {
                recording_ = true;
                num_samps_ = 0;
                wptr_      = 0;
                bstate_    = BufferState::IDLE;
                memset(transfer_buff, 0, sizeof(transfer_buff));
            }
        }
    }

    /** Returns whether recording is currently active or not. */
    inline bool IsRecording() const { return recording_; }

    /** Returns the current length in samples of the recording. */
    inline uint32_t GetLengthSamps() { return num_samps_; }

    /** Returns the current length of the recording in seconds. */
    inline float GetLengthSeconds()
    {
        return (float)num_samps_ / (float)cfg_.samplerate;
    }

  private:
    /** Calculate the file size based on current recording */
    inline uint32_t CalcFileSize()
    {
        wavheader_.SubCHunk2Size
            = num_samps_ * cfg_.channels * cfg_.bitspersample / 8;
        return 36 + wavheader_.SubCHunk2Size;
    }

    /** Compute the byte rate given the user settings. */
    inline uint32_t CalcByteRate()
    {
        return cfg_.samplerate * cfg_.channels * cfg_.bitspersample / 8;
    }

    static constexpr int kTransferSamps = transfer_size / sizeof(int32_t);

    WAV_FormatTypeDef wavheader_;
    uint32_t          num_samps_, wptr_;
    Config            cfg_;
    int32_t           transfer_buff[kTransferSamps * 2];
    BufferState       bstate_;
    bool              recording_;
    FIL               fp_;
};

} // namespace daisy
