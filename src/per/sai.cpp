#include "per/sai.h"
#include "daisy_core.h"
extern "C"
{
#include "util/hal_map.h"
}

namespace daisy
{
class SaiHandle::Impl
{
  public:
    enum class PeripheralBlock
    {
        BLOCK_A,
        BLOCK_B,
    };

    SaiHandle::Result        Init(const SaiHandle::Config& config);
    SaiHandle::Result        DeInit();
    const SaiHandle::Config& GetConfig() const { return config_; }

    SaiHandle::Result StartDmaTransfer(int32_t*                       buffer_rx,
                                       int32_t*                       buffer_tx,
                                       size_t                         size,
                                       SaiHandle::CallbackFunctionPtr callback);
    SaiHandle::Result StopDmaTransfer();

    // Utility functions
    float  GetSampleRate();
    size_t GetBlockSize();
    float  GetBlockRate();

    SaiHandle::Config config_;
    SAI_HandleTypeDef sai_a_handle_, sai_b_handle_;
    DMA_HandleTypeDef sai_a_dma_handle_, sai_b_dma_handle_;

    // Data kept for Callback usage
    int32_t *                      buff_rx_, *buff_tx_;
    size_t                         buff_size_;
    SaiHandle::CallbackFunctionPtr callback_;

    /** Offset stored for weird inter-SAI stuff.*/
    size_t dma_offset;

    /** Callback that dispatches user callback from Cplt and HalfCplt DMA Callbacks */
    void InternalCallback(size_t offset);

    /** Pin Initiazlization */
    void InitPins();
    void DeInitPins();

    /** DMA Initialization */
    void InitDma(PeripheralBlock block);
    void DeInitDma(PeripheralBlock block);
};

// ================================================================
// Generic Error Handler
// ================================================================

static void Error_Handler()
{
    asm("bkpt 255");
    while(1) {}
}

// ================================================================
// Static References for available SaiHandle::Impls
// ================================================================

static SaiHandle::Impl sai_handles[2];

// ================================================================
// SAI Functions
// ================================================================

SaiHandle::Result SaiHandle::Impl::Init(const SaiHandle::Config& config)
{
    // do some init stuff
    const int sai_idx = int(config.periph);
    if(sai_idx >= 2)
        return Result::ERR;

    // Default Buffer states
    buff_rx_   = nullptr;
    buff_tx_   = nullptr;
    buff_size_ = 0;
    config_    = config;

    constexpr SAI_Block_TypeDef* a_instances[2] = {SAI1_Block_A, SAI2_Block_A};
    constexpr SAI_Block_TypeDef* b_instances[2] = {SAI1_Block_B, SAI2_Block_B};

    sai_a_handle_.Instance = a_instances[sai_idx];
    sai_b_handle_.Instance = b_instances[sai_idx];

    // Samplerate
    switch(config.sr)
    {
        case Config::SampleRate::SAI_8KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_8K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_8K;
            break;
        case Config::SampleRate::SAI_16KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_16K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_16K;
            break;
        case Config::SampleRate::SAI_32KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_32K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_32K;
            break;
        case Config::SampleRate::SAI_48KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
            break;
        case Config::SampleRate::SAI_96KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_96K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_96K;
            break;
        default: break;
    }
    // Audio Mode A
    if(config.a_sync == Config::Sync::MASTER)
    {
        sai_a_handle_.Init.AudioMode
            = config.a_dir == Config::Direction::TRANSMIT ? SAI_MODEMASTER_TX
                                                          : SAI_MODEMASTER_RX;
        sai_a_handle_.Init.Synchro = SAI_ASYNCHRONOUS;
    }
    else
    {
        sai_a_handle_.Init.AudioMode
            = config.a_dir == Config::Direction::TRANSMIT ? SAI_MODESLAVE_TX
                                                          : SAI_MODESLAVE_RX;
        sai_a_handle_.Init.Synchro = SAI_SYNCHRONOUS;
    }
    // Audio Mode B
    if(config.b_sync == Config::Sync::MASTER)
    {
        sai_b_handle_.Init.AudioMode
            = config.b_dir == Config::Direction::TRANSMIT ? SAI_MODEMASTER_TX
                                                          : SAI_MODEMASTER_RX;
        sai_b_handle_.Init.Synchro = SAI_ASYNCHRONOUS;
    }
    else
    {
        sai_b_handle_.Init.AudioMode
            = config.b_dir == Config::Direction::TRANSMIT ? SAI_MODESLAVE_TX
                                                          : SAI_MODESLAVE_RX;
        sai_b_handle_.Init.Synchro = SAI_SYNCHRONOUS;
    }
    // Bitdepth / protocol (currently based on bitdepth..)
    // TODO probably split these up for better flexibility..
    // These are also currently fixed to be the same per block.
    uint8_t  bd;
    uint32_t protocol;
    switch(config.bit_depth)
    {
        case Config::BitDepth::SAI_16BIT:
            bd       = SAI_PROTOCOL_DATASIZE_16BIT;
            protocol = SAI_I2S_STANDARD;
            break;
        case Config::BitDepth::SAI_24BIT:
            bd       = SAI_PROTOCOL_DATASIZE_24BIT;
            protocol = SAI_I2S_MSBJUSTIFIED;
            break;
        case Config::BitDepth::SAI_32BIT:
            // Untested Configuration
            bd       = SAI_PROTOCOL_DATASIZE_32BIT;
            protocol = SAI_I2S_STANDARD;
            break;
        default: break;
    }

    // Generic Inits that we don't have API control over.
    // A
    sai_a_handle_.Init.OutputDrive    = SAI_OUTPUTDRIVE_DISABLE;
    sai_a_handle_.Init.NoDivider      = SAI_MASTERDIVIDER_ENABLE;
    sai_a_handle_.Init.FIFOThreshold  = SAI_FIFOTHRESHOLD_EMPTY;
    sai_a_handle_.Init.SynchroExt     = SAI_SYNCEXT_DISABLE;
    sai_a_handle_.Init.MonoStereoMode = SAI_STEREOMODE;
    sai_a_handle_.Init.CompandingMode = SAI_NOCOMPANDING;
    sai_a_handle_.Init.TriState       = SAI_OUTPUT_NOTRELEASED;
    // B
    sai_b_handle_.Init.OutputDrive    = SAI_OUTPUTDRIVE_DISABLE;
    sai_b_handle_.Init.NoDivider      = SAI_MASTERDIVIDER_ENABLE;
    sai_b_handle_.Init.FIFOThreshold  = SAI_FIFOTHRESHOLD_EMPTY;
    sai_b_handle_.Init.SynchroExt     = SAI_SYNCEXT_DISABLE;
    sai_b_handle_.Init.MonoStereoMode = SAI_STEREOMODE;
    sai_b_handle_.Init.CompandingMode = SAI_NOCOMPANDING;
    sai_b_handle_.Init.TriState       = SAI_OUTPUT_NOTRELEASED;
    if(HAL_SAI_InitProtocol(&sai_a_handle_, protocol, bd, 2) != HAL_OK)
    {
        Error_Handler();
        return Result::ERR;
    }

    if(HAL_SAI_InitProtocol(&sai_b_handle_, protocol, bd, 2) != HAL_OK)
    {
        Error_Handler();
        return Result::ERR;
    }

    return Result::OK;
}

SaiHandle::Result SaiHandle::Impl::DeInit()
{
    // Must have been initialized before deinitialization
    if(&config_ == nullptr)
        return Result::ERR;

    DeInitDma(PeripheralBlock::BLOCK_A);
    DeInitDma(PeripheralBlock::BLOCK_B);

    if(HAL_SAI_DeInit(&sai_a_handle_) != HAL_OK)
        return Result::ERR;
    if(HAL_SAI_DeInit(&sai_b_handle_) != HAL_OK)
        return Result::ERR;

    return Result::OK;
}

void SaiHandle::Impl::InitDma(PeripheralBlock block)
{
    SAI_HandleTypeDef* hsai;
    DMA_HandleTypeDef* hdma;
    uint32_t           req, dir;

    const int sai_idx = int(config_.periph);

    if(block == PeripheralBlock::BLOCK_A)
    {
        hsai = &sai_a_handle_;
        hdma = &sai_a_dma_handle_;
        dir  = config_.a_dir == Config::Direction::RECEIVE
                  ? DMA_PERIPH_TO_MEMORY
                  : DMA_MEMORY_TO_PERIPH;
        req = sai_idx == int(Config::Peripheral::SAI_1) ? DMA_REQUEST_SAI1_A
                                                        : DMA_REQUEST_SAI2_A;

        if(sai_idx == int(Config::Peripheral::SAI_1))
            hdma->Instance = DMA1_Stream0;
        else
            hdma->Instance = DMA1_Stream3;
    }
    else
    {
        hsai = &sai_b_handle_;
        hdma = &sai_b_dma_handle_;
        dir  = config_.b_dir == Config::Direction::RECEIVE
                  ? DMA_PERIPH_TO_MEMORY
                  : DMA_MEMORY_TO_PERIPH;
        req = sai_idx == int(Config::Peripheral::SAI_1) ? DMA_REQUEST_SAI1_B
                                                        : DMA_REQUEST_SAI2_B;

        if(sai_idx == int(Config::Peripheral::SAI_1))
            hdma->Instance = DMA1_Stream1;
        else
            hdma->Instance = DMA1_Stream4;
    }

    // Generic
    hdma->Init.Request             = req;
    hdma->Init.Direction           = dir;
    hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma->Init.MemInc              = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma->Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    hdma->Init.Mode                = DMA_CIRCULAR;
    hdma->Init.Priority            = DMA_PRIORITY_HIGH;
    hdma->Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(hdma) != HAL_OK)
    {
        Error_Handler();
    }
    __HAL_LINKDMA(hsai, hdmarx, *hdma);
    __HAL_LINKDMA(hsai, hdmatx, *hdma);
}

void SaiHandle::Impl::DeInitDma(PeripheralBlock block)
{
    if(block == PeripheralBlock::BLOCK_A)
    {
        HAL_DMA_DeInit(sai_a_handle_.hdmarx);
        HAL_DMA_DeInit(sai_a_handle_.hdmatx);
    }
    else if(block == PeripheralBlock::BLOCK_B)
    {
        HAL_DMA_DeInit(sai_b_handle_.hdmarx);
        HAL_DMA_DeInit(sai_b_handle_.hdmatx);
    }
}

void SaiHandle::Impl::InternalCallback(size_t offset)
{
    int32_t *in, *out;
    in  = buff_rx_ + offset;
    out = buff_tx_ + offset;
    if(callback_)
        callback_(in, out, buff_size_ / 2);
}

SaiHandle::Result
SaiHandle::Impl::StartDmaTransfer(int32_t*                       buffer_rx,
                                  int32_t*                       buffer_tx,
                                  size_t                         size,
                                  SaiHandle::CallbackFunctionPtr callback)
{
    buff_rx_   = buffer_rx;
    buff_tx_   = buffer_tx;
    buff_size_ = size;
    callback_  = callback;

    // This assumes there will be one master and one slave
    if(config_.a_sync == Config::Sync::SLAVE)
    {
        config_.a_dir == Config::Direction::RECEIVE
            ? HAL_SAI_Receive_DMA(&sai_a_handle_, (uint8_t*)buffer_rx, size)
            : HAL_SAI_Transmit_DMA(&sai_a_handle_, (uint8_t*)buffer_tx, size);
        config_.b_dir == Config::Direction::RECEIVE
            ? HAL_SAI_Receive_DMA(&sai_b_handle_, (uint8_t*)buffer_rx, size)
            : HAL_SAI_Transmit_DMA(&sai_b_handle_, (uint8_t*)buffer_tx, size);
    }
    else
    {
        config_.b_dir == Config::Direction::RECEIVE
            ? HAL_SAI_Receive_DMA(&sai_b_handle_, (uint8_t*)buffer_rx, size)
            : HAL_SAI_Transmit_DMA(&sai_b_handle_, (uint8_t*)buffer_tx, size);
        config_.a_dir == Config::Direction::RECEIVE
            ? HAL_SAI_Receive_DMA(&sai_a_handle_, (uint8_t*)buffer_rx, size)
            : HAL_SAI_Transmit_DMA(&sai_a_handle_, (uint8_t*)buffer_tx, size);
    }

    return Result::OK;
}
SaiHandle::Result SaiHandle::Impl::StopDmaTransfer()
{
    HAL_SAI_DMAStop(&sai_a_handle_);
    HAL_SAI_DMAStop(&sai_b_handle_);
    return Result::OK;
}

float SaiHandle::Impl::GetSampleRate()
{
    switch(config_.sr)
    {
        case Config::SampleRate::SAI_8KHZ: return 8000.f;
        case Config::SampleRate::SAI_16KHZ: return 16000.f;
        case Config::SampleRate::SAI_32KHZ: return 32000.f;
        case Config::SampleRate::SAI_48KHZ: return 48000.f;
        case Config::SampleRate::SAI_96KHZ: return 96000.f;
        default: return 48000.f;
    }
}
size_t SaiHandle::Impl::GetBlockSize()
{
    // Buffer handled in halves, 2 samples per frame (1 per channel)
    return buff_size_ / 2 / 2;
}
float SaiHandle::Impl::GetBlockRate()
{
    return GetSampleRate() / GetBlockSize();
}

void SaiHandle::Impl::InitPins()
{
    bool             is_master;
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_TypeDef*    port;
    dsy_gpio_pin*    cfg[] = {&config_.pin_config.fs,
                           &config_.pin_config.mclk,
                           &config_.pin_config.sck,
                           &config_.pin_config.sa,
                           &config_.pin_config.sb};
    // Special Case checks
    dsy_gpio_pin sck_af_pin = {DSY_GPIOA, 2};
    is_master               = (config_.a_sync == Config::Sync::MASTER
                 || config_.b_sync == Config::Sync::MASTER);
    // Generics
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    for(size_t i = 0; i < 5; i++)
    {
        // Skip MCLK if not master.
        if(dsy_pin_cmp(&config_.pin_config.mclk, cfg[i]) && !is_master)
            continue;

        // Special case for SAI2-sck (should add a map for Alternate Functions at some point..)
        switch(config_.periph)
        {
            case Config::Peripheral::SAI_1:
                GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
                break;
            case Config::Peripheral::SAI_2:
                GPIO_InitStruct.Alternate = dsy_pin_cmp(cfg[i], &sck_af_pin)
                                                ? GPIO_AF8_SAI2
                                                : GPIO_AF10_SAI2;
                break;
            default: break;
        }

        GPIO_InitStruct.Pin = dsy_hal_map_get_pin(cfg[i]);
        port                = dsy_hal_map_get_port(cfg[i]);
        HAL_GPIO_Init(port, &GPIO_InitStruct);
    }
}

void SaiHandle::Impl::DeInitPins()
{
    GPIO_TypeDef* port;
    uint16_t      pin;
    bool          is_master;
    is_master = (config_.a_sync == Config::Sync::MASTER
                 || config_.b_sync == Config::Sync::MASTER);
    if(is_master)
    {
        port = dsy_hal_map_get_port(&config_.pin_config.mclk);
        pin  = dsy_hal_map_get_pin(&config_.pin_config.mclk);
        HAL_GPIO_DeInit(port, pin);
    }
    port = dsy_hal_map_get_port(&config_.pin_config.fs);
    pin  = dsy_hal_map_get_pin(&config_.pin_config.fs);
    HAL_GPIO_DeInit(port, pin);
    port = dsy_hal_map_get_port(&config_.pin_config.sck);
    pin  = dsy_hal_map_get_pin(&config_.pin_config.sck);
    HAL_GPIO_DeInit(port, pin);
    port = dsy_hal_map_get_port(&config_.pin_config.sa);
    pin  = dsy_hal_map_get_pin(&config_.pin_config.sa);
    HAL_GPIO_DeInit(port, pin);
    port = dsy_hal_map_get_port(&config_.pin_config.sb);
    pin  = dsy_hal_map_get_pin(&config_.pin_config.sb);
    HAL_GPIO_DeInit(port, pin);
}

// ================================================================
// HAL Service Functions
// ================================================================

extern "C" void HAL_SAI_MspInit(SAI_HandleTypeDef* hsai)
{
    // Due to the BlockA/BlockB stuff right now
    // it is required that they both be used.
    // We could add a layer of separate config for them.
    if(hsai->Instance == SAI1_Block_A || hsai->Instance == SAI1_Block_B)
    {
        // Since this isn't using the daisy gpio yet, we need to manually start the necessary pclks
        // these aren't all used on all hardware, but they cover existing hardware so far.
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOE_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_SAI1_CLK_ENABLE();
        sai_handles[0].InitPins();
        __HAL_RCC_DMA1_CLK_ENABLE();
        sai_handles[0].InitDma(hsai->Instance == SAI1_Block_A
                                   ? SaiHandle::Impl::PeripheralBlock::BLOCK_A
                                   : SaiHandle::Impl::PeripheralBlock::BLOCK_B);
    }
    else if(hsai->Instance == SAI2_Block_A || hsai->Instance == SAI2_Block_B)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_SAI2_CLK_ENABLE();
        sai_handles[1].InitPins();
        __HAL_RCC_DMA1_CLK_ENABLE();
        sai_handles[1].InitDma(hsai->Instance == SAI2_Block_A
                                   ? SaiHandle::Impl::PeripheralBlock::BLOCK_A
                                   : SaiHandle::Impl::PeripheralBlock::BLOCK_B);
    }
}
extern "C" void HAL_SAI_MspDeInit(SAI_HandleTypeDef* hsai)
{
    if(hsai->Instance == SAI1_Block_A)
    {
        __HAL_RCC_SAI1_CLK_DISABLE();
        sai_handles[0].DeInitPins();
    }
    else if(hsai->Instance == SAI2_Block_A)
    {
        __HAL_RCC_SAI2_CLK_DISABLE();
        sai_handles[1].DeInitPins();
    }
}

// ================================================================
// ISRs and event handlers
// ================================================================

extern "C" void DMA1_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sai_handles[0].sai_a_dma_handle_);
}

extern "C" void DMA1_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sai_handles[0].sai_b_dma_handle_);
}

extern "C" void DMA1_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sai_handles[1].sai_a_dma_handle_);
}

extern "C" void DMA1_Stream4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sai_handles[1].sai_b_dma_handle_);
}

extern "C" void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef* hsai)
{
    if(hsai->Instance == SAI1_Block_A || hsai->Instance == SAI1_Block_B)
    {
        sai_handles[0].dma_offset = 0;
        sai_handles[0].InternalCallback(0);
    }
    else if(hsai->Instance == SAI2_Block_A || hsai->Instance == SAI2_Block_B)
    {
        sai_handles[1].dma_offset = 0;
        sai_handles[1].InternalCallback(0);
    }
}

extern "C" void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef* hsai)
{
    if(hsai->Instance == SAI1_Block_A || hsai->Instance == SAI1_Block_B)
    {
        sai_handles[0].dma_offset = sai_handles[0].buff_size_ / 2;
        sai_handles[0].InternalCallback(sai_handles[0].dma_offset);
    }
    else if(hsai->Instance == SAI2_Block_A || hsai->Instance == SAI2_Block_B)
    {
        sai_handles[1].dma_offset = sai_handles[1].buff_size_ / 2;
        sai_handles[1].InternalCallback(sai_handles[1].dma_offset);
    }
}

// ================================================================
// SaiHandle -> SaiHandle::Pimpl
// ================================================================

SaiHandle::Result SaiHandle::Init(const Config& config)
{
    pimpl_ = &sai_handles[int(config.periph)];
    return pimpl_->Init(config);
}
SaiHandle::Result SaiHandle::DeInit()
{
    return pimpl_->DeInit();
}
const SaiHandle::Config& SaiHandle::GetConfig() const
{
    return pimpl_->GetConfig();
}

SaiHandle::Result SaiHandle::StartDma(int32_t*            buffer_rx,
                                      int32_t*            buffer_tx,
                                      size_t              size,
                                      CallbackFunctionPtr callback)
{
    return pimpl_->StartDmaTransfer(buffer_rx, buffer_tx, size, callback);
}

SaiHandle::Result SaiHandle::StopDma()
{
    return pimpl_->StopDmaTransfer();
}

float SaiHandle::GetSampleRate()
{
    return pimpl_->GetSampleRate();
}

size_t SaiHandle::GetBlockSize()
{
    return pimpl_->GetBlockSize();
}

float SaiHandle::GetBlockRate()
{
    return pimpl_->GetBlockRate();
}

size_t SaiHandle::GetOffset() const
{
    return pimpl_->dma_offset;
}


} // namespace daisy
