#include "hid/audio.h"

namespace daisy
{
// ================================================================
// Static Globals
// ================================================================

// Ordinarily the user would be required to provide their own buffer, but
// in the interest in encourage newcomers, and this also being an audio-centric platform
// these buffers will always be present, and usable.
//
static const size_t kAudioMaxBufferSize = 1024;
static const size_t kAudioMaxChannels   = 4;

// Static Global Buffers
// 8kB in SRAM1, non-cached memory
// 1k samples in, 1k samples out, 4 bytes per sample.
// One buffer per 2 channels (Interleaved on hardware)
static int32_t DMA_BUFFER_MEM_SECTION
    dsy_audio_rx_buffer[kAudioMaxChannels / 2][kAudioMaxBufferSize];
static int32_t DMA_BUFFER_MEM_SECTION
    dsy_audio_tx_buffer[kAudioMaxChannels / 2][kAudioMaxBufferSize];

// ================================================================
// Private Implementation Definition
// ================================================================
class AudioHandle::Impl
{
  public:
    // Interface
    AudioHandle::Result Init(const AudioHandle::Config config, SaiHandle sai);
    AudioHandle::Result
                        Init(const AudioHandle::Config config, SaiHandle sai1, SaiHandle sai2);
    AudioHandle::Result DeInit();
    AudioHandle::Result Start(AudioHandle::AudioCallback callback);
    AudioHandle::Result Start(AudioHandle::InterleavingAudioCallback callback);
    AudioHandle::Result Stop();
    AudioHandle::Result ChangeCallback(AudioHandle::AudioCallback callback);
    AudioHandle::Result
    ChangeCallback(AudioHandle::InterleavingAudioCallback callback);

    inline size_t GetChannels() const
    {
        if(sai1_.IsInitialized() && sai2_.IsInitialized())
            return 4;
        else if(sai1_.IsInitialized() || sai2_.IsInitialized())
            return 2;
        else
            return 0;
    }

    AudioHandle::Result SetBlockSize(size_t size)
    {
        size_t maxSize    = kAudioMaxBufferSize / 4;
        config_.blocksize = size <= maxSize ? size : maxSize;
        return size <= maxSize ? AudioHandle::Result::OK
                               : AudioHandle::Result::ERR;
    }

    float GetSampleRate() { return sai1_.GetSampleRate(); }

    AudioHandle::Result SetPostGain(float val)
    {
        if(val <= 0.f)
            return AudioHandle::Result::ERR;
        config_.postgain = val;
        // Precompute input adjust
        postgain_recip_ = 1.f / config_.postgain;
        // Precompute output adjust
        output_adjust_ = config_.postgain * config_.output_compensation;
        return AudioHandle::Result::OK;
    }

    AudioHandle::Result SetOutputCompensation(float val)
    {
        config_.output_compensation = val;
        // recompute output adjustment (no need to recompute input adjust here)
        output_adjust_ = config_.output_compensation * config_.postgain;
        return AudioHandle::Result::OK;
    }

    AudioHandle::Result SetSampleRate(SaiHandle::Config::SampleRate sampelrate);

    // Internal Callback
    static void InternalCallback(int32_t* in, int32_t* out, size_t size);

    void *callback_, *interleaved_callback_;

    // Data
    AudioHandle::Config config_;
    SaiHandle           sai1_, sai2_;
    int32_t*            buff_rx_[2];
    int32_t*            buff_tx_[2];
    float               postgain_recip_;
    float               output_adjust_;
};

// ================================================================
// Static Reference for Object
// ================================================================

static AudioHandle::Impl audio_handle;

// ================================================================
// Private Implementation
// ================================================================

AudioHandle::Result AudioHandle::Impl::Init(const AudioHandle::Config config,
                                            SaiHandle                 sai)
{
    config_ = config;

    /** Precompute input level adjustment */
    if(config_.postgain > 0.f)
        postgain_recip_ = 1.f / config_.postgain;
    else
        return Result::ERR;

    /** Precompute output level adjustment */
    output_adjust_ = config_.postgain * config_.output_compensation;

    if(sai.IsInitialized())
    {
        sai1_              = sai;
        config_.samplerate = sai1_.GetConfig().sr;
    }
    else
    {
        return Result::ERR;
    }
    buff_rx_[0] = dsy_audio_rx_buffer[0];
    buff_tx_[0] = dsy_audio_tx_buffer[0];
    return Result::OK;
}

AudioHandle::Result AudioHandle::Impl::Init(const AudioHandle::Config config,
                                            SaiHandle                 sai1,
                                            SaiHandle                 sai2)
{
    this->Init(config, sai1);
    sai2_       = sai2;
    buff_rx_[1] = dsy_audio_rx_buffer[1];
    buff_tx_[1] = dsy_audio_tx_buffer[1];
    // How do we want to handle the rx/tx buffs for the second peripheral of audio..?
    return Result::OK;
}

AudioHandle::Result AudioHandle::Impl::DeInit()
{
    Stop();
    if(sai1_.IsInitialized())
    {
        if(sai1_.DeInit() != SaiHandle::Result::OK)
        {
            return Result::ERR;
        }
    }
    if(sai2_.IsInitialized())
    {
        if(sai2_.DeInit() != SaiHandle::Result::OK)
        {
            return Result::ERR;
        }
    }
    return Result::OK;
}

AudioHandle::Result
AudioHandle::Impl::Start(AudioHandle::AudioCallback callback)
{
    // Get instance of object
    if(sai2_.IsInitialized())
    {
        // Start stream with no callback. Data will be filled externally.
        sai2_.StartDma(buff_rx_[0],
                       buff_tx_[0],
                       config_.blocksize * 2 * 2,
                       audio_handle.InternalCallback);

        sai1_.StartDma(
            buff_rx_[1], buff_tx_[1], config_.blocksize * 2 * 2, nullptr);
    }
    else
    {
        sai1_.StartDma(buff_rx_[0],
                       buff_tx_[0],
                       config_.blocksize * 2 * 2,
                       audio_handle.InternalCallback);
    }
    callback_             = (void*)callback;
    interleaved_callback_ = nullptr;
    return Result::OK;
}

AudioHandle::Result
AudioHandle::Impl::Start(AudioHandle::InterleavingAudioCallback callback)
{
    // Get instance of object
    sai1_.StartDma(buff_rx_[0],
                   buff_tx_[0],
                   config_.blocksize * 2 * 2,
                   audio_handle.InternalCallback);
    interleaved_callback_ = (void*)callback;
    callback_             = nullptr;
    return Result::OK;
}

AudioHandle::Result AudioHandle::Impl::Stop()
{
    if(sai1_.IsInitialized())
        sai1_.StopDma();
    if(sai2_.IsInitialized())
        sai2_.StopDma();
    return Result::OK;
}

AudioHandle::Result
AudioHandle::Impl::ChangeCallback(AudioHandle::AudioCallback callback)
{
    if(callback != nullptr)
    {
        callback_             = (void*)callback;
        interleaved_callback_ = nullptr;
        return Result::OK;
    }
    else
    {
        return Result::ERR;
    }
}

AudioHandle::Result AudioHandle::Impl::ChangeCallback(
    AudioHandle::InterleavingAudioCallback callback)
{
    if(callback != nullptr)
    {
        interleaved_callback_ = (void*)callback;
        callback_             = nullptr;
        return Result::OK;
    }
    else
    {
        return Result::ERR;
    }
}

AudioHandle::Result
AudioHandle::Impl::SetSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    config_.samplerate = samplerate;
    if(sai1_.IsInitialized())
    {
        // Set, and reinit
        SaiHandle::Config cfg;
        cfg    = sai1_.GetConfig();
        cfg.sr = config_.samplerate;
        if(sai1_.Init(cfg) != SaiHandle::Result::OK)
        {
            return Result::ERR;
        }
    }
    if(sai2_.IsInitialized())
    {
        // Set, and reinit
        SaiHandle::Config cfg;
        cfg    = sai2_.GetConfig();
        cfg.sr = config_.samplerate;
        if(sai2_.Init(cfg) != SaiHandle::Result::OK)
        {
            return Result::ERR;
        }
    }
    return Result::OK;
}

// This turned into a very large function due to the bit-depth conversions..
// I didn't want to do a conditional conversion in a single loop because it seemed
// far less efficient, so this is where I landed.
//
// Using function pointers for the x2f and f2x functions would have been ideal, but
// wasn't possible due to the different parameter/return types for each function.
void AudioHandle::Impl::InternalCallback(int32_t* in, int32_t* out, size_t size)
{
    // Convert from sai format to float, and call user callback
    size_t                      chns;
    SaiHandle::Config::BitDepth bd;
    bd   = audio_handle.sai1_.GetConfig().bit_depth;
    chns = audio_handle.GetChannels();
    if(chns == 0)
        return;
    // Handle Interleaved / Non Interleaved separate
    if(audio_handle.interleaved_callback_)
    {
        InterleavingAudioCallback cb
            = (InterleavingAudioCallback)audio_handle.interleaved_callback_;
        float fin[size];
        float fout[size];
        // There _must_ be a more elegant way to do this....
        // Convert from int to float
        switch(bd)
        {
            case SaiHandle::Config::BitDepth::SAI_16BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    fin[i] = s162f(in[i]) * audio_handle.postgain_recip_;
                    fin[i + 1]
                        = s162f(in[i + 1]) * audio_handle.postgain_recip_;
                }
                break;
            case SaiHandle::Config::BitDepth::SAI_24BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    fin[i] = s242f(in[i]) * audio_handle.postgain_recip_;
                    fin[i + 1]
                        = s242f(in[i + 1]) * audio_handle.postgain_recip_;
                }
                break;
            case SaiHandle::Config::BitDepth::SAI_32BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    fin[i] = s322f(in[i]) * audio_handle.postgain_recip_;
                    fin[i + 1]
                        = s322f(in[i + 1]) * audio_handle.postgain_recip_;
                }
                break;
            default: break;
        }
        cb(fin, fout, size);
        switch(bd)
        {
            case SaiHandle::Config::BitDepth::SAI_16BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    out[i] = f2s16(fout[i] * audio_handle.output_adjust_);
                    out[i + 1]
                        = f2s16(fout[i + 1] * audio_handle.output_adjust_);
                }
                break;
            case SaiHandle::Config::BitDepth::SAI_24BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    out[i] = f2s24(fout[i] * audio_handle.output_adjust_);
                    out[i + 1]
                        = f2s24(fout[i + 1] * audio_handle.output_adjust_);
                }
                break;
            case SaiHandle::Config::BitDepth::SAI_32BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    out[i] = f2s32(fout[i] * audio_handle.output_adjust_);
                    out[i + 1]
                        = f2s32(fout[i + 1] * audio_handle.output_adjust_);
                }
                break;
            default: break;
        }
    }
    else if(audio_handle.callback_)
    {
        AudioCallback cb = (AudioCallback)audio_handle.callback_;
        // offset needed for 2nd audio codec.
        size_t offset    = audio_handle.sai2_.GetOffset();
        size_t buff_size = chns > 2 ? size * 2 : size;
        float  finbuff[buff_size], foutbuff[buff_size];
        float* fin[chns];
        float* fout[chns];
        fin[0]  = finbuff;
        fin[1]  = finbuff + (buff_size / chns);
        fout[0] = foutbuff;
        fout[1] = foutbuff + (buff_size / chns);
        if(chns > 2)
        {
            fin[2]  = fin[1] + (buff_size / chns);
            fin[3]  = fin[2] + (buff_size / chns);
            fout[2] = fout[1] + (buff_size / chns);
            fout[3] = fout[2] + (buff_size / chns);
        }
        // Deinterleave and scale
        switch(bd)
        {
            case SaiHandle::Config::BitDepth::SAI_16BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    fin[0][i / 2] = s162f(in[i]) * audio_handle.postgain_recip_;
                    fin[1][i / 2]
                        = s162f(in[i + 1]) * audio_handle.postgain_recip_;
                    if(chns > 2)
                    {
                        fin[2][i / 2]
                            = s162f(audio_handle.buff_rx_[1][offset + i])
                              * audio_handle.postgain_recip_;
                        fin[3][i / 2]
                            = s162f(audio_handle.buff_rx_[1][offset + i + 1])
                              * audio_handle.postgain_recip_;
                    }
                }
                break;
            case SaiHandle::Config::BitDepth::SAI_24BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    fin[0][i / 2] = s242f(in[i]) * audio_handle.postgain_recip_;
                    fin[1][i / 2]
                        = s242f(in[i + 1]) * audio_handle.postgain_recip_;
                    if(chns > 2)
                    {
                        fin[2][i / 2]
                            = s242f(audio_handle.buff_rx_[1][offset + i])
                              * audio_handle.postgain_recip_;
                        fin[3][i / 2]
                            = s242f(audio_handle.buff_rx_[1][offset + i + 1])
                              * audio_handle.postgain_recip_;
                    }
                }
                break;
            case SaiHandle::Config::BitDepth::SAI_32BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    fin[0][i / 2] = s322f(in[i]) * audio_handle.postgain_recip_;
                    fin[1][i / 2]
                        = s322f(in[i + 1]) * audio_handle.postgain_recip_;
                    if(chns > 2)
                    {
                        fin[2][i / 2]
                            = s322f(audio_handle.buff_rx_[1][offset + i])
                              * audio_handle.postgain_recip_;
                        fin[3][i / 2]
                            = s322f(audio_handle.buff_rx_[1][offset + i + 1])
                              * audio_handle.postgain_recip_;
                    }
                }
                break;
            default: break;
        }
        if(chns > 2)
        {
            // swap inputs
            fin[2] = fin[0];
            fin[3] = fin[1];
            fin[0] = fin[1] + (buff_size / chns);
            fin[1] = fin[0] + (buff_size / chns);
        }
        cb(fin, fout, size / 2);
        if(chns > 2)
        {
            // swap outputs
            fout[2] = fout[0];
            fout[3] = fout[1];
            fout[0] = fout[1] + (buff_size / chns);
            fout[1] = fout[0] + (buff_size / chns);
        }
        // Reinterleave and scale
        switch(bd)
        {
            case SaiHandle::Config::BitDepth::SAI_16BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    out[i]
                        = f2s16(fout[0][i / 2] * audio_handle.output_adjust_);
                    out[i + 1]
                        = f2s16(fout[1][i / 2] * audio_handle.output_adjust_);
                    if(chns > 2)
                    {
                        audio_handle.buff_tx_[1][offset + i] = f2s16(
                            fout[2][i / 2] * audio_handle.output_adjust_);
                        audio_handle.buff_tx_[1][offset + i + 1] = f2s16(
                            fout[3][i / 2] * audio_handle.output_adjust_);
                    }
                }
                break;
            case SaiHandle::Config::BitDepth::SAI_24BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    out[i]
                        = f2s24(fout[0][i / 2] * audio_handle.output_adjust_);
                    out[i + 1]
                        = f2s24(fout[1][i / 2] * audio_handle.output_adjust_);
                    if(chns > 2)
                    {
                        audio_handle.buff_tx_[1][offset + i] = f2s24(
                            fout[2][i / 2] * audio_handle.output_adjust_);
                        audio_handle.buff_tx_[1][offset + i + 1] = f2s24(
                            fout[3][i / 2] * audio_handle.output_adjust_);
                    }
                }
                break;
            case SaiHandle::Config::BitDepth::SAI_32BIT:
                for(size_t i = 0; i < size; i += 2)
                {
                    out[i]
                        = f2s32(fout[0][i / 2] * audio_handle.output_adjust_);
                    out[i + 1]
                        = f2s32(fout[1][i / 2] * audio_handle.output_adjust_);
                    if(chns > 2)
                    {
                        audio_handle.buff_tx_[1][offset + i] = f2s32(
                            fout[2][i / 2] * audio_handle.output_adjust_);
                        audio_handle.buff_tx_[1][offset + i + 1] = f2s32(
                            fout[3][i / 2] * audio_handle.output_adjust_);
                    }
                }
                break;
            default: break;
        }
    }
}

// ================================================================
// SaiHandle -> SaiHandle::Pimpl
// ================================================================

AudioHandle::Result AudioHandle::Init(const Config& config, SaiHandle sai)
{
    // Figure out proper pattern for singleton behavior here.
    pimpl_ = &audio_handle;
    return pimpl_->Init(config, sai);
}

AudioHandle::Result
AudioHandle::Init(const Config& config, SaiHandle sai1, SaiHandle sai2)
{
    // Figure out proper pattern for singleton behavior here.
    pimpl_ = &audio_handle;
    return pimpl_->Init(config, sai1, sai2);
}

AudioHandle::Result AudioHandle::DeInit()
{
    return pimpl_->DeInit();
}

const AudioHandle::Config& AudioHandle::GetConfig() const
{
    return pimpl_->config_;
}

size_t AudioHandle::GetChannels() const
{
    return pimpl_->GetChannels();
}

AudioHandle::Result AudioHandle::SetBlockSize(size_t size)
{
    return pimpl_->SetBlockSize(size);
}

float AudioHandle::GetSampleRate()
{
    return pimpl_->GetSampleRate();
}

AudioHandle::Result
AudioHandle::SetSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    return pimpl_->SetSampleRate(samplerate);
}

AudioHandle::Result AudioHandle::Start(AudioCallback callback)
{
    return pimpl_->Start(callback);
}

AudioHandle::Result AudioHandle::Start(InterleavingAudioCallback callback)
{
    return pimpl_->Start(callback);
}

AudioHandle::Result AudioHandle::Stop()
{
    return pimpl_->Stop();
}

AudioHandle::Result AudioHandle::ChangeCallback(AudioCallback callback)
{
    return pimpl_->ChangeCallback(callback);
}

AudioHandle::Result
AudioHandle::ChangeCallback(InterleavingAudioCallback callback)
{
    return pimpl_->ChangeCallback(callback);
}

AudioHandle::Result AudioHandle::SetPostGain(float val)
{
    return pimpl_->SetPostGain(val);
}

AudioHandle::Result AudioHandle::SetOutputCompensation(float val)
{
    return pimpl_->SetOutputCompensation(val);
}

} // namespace daisy
