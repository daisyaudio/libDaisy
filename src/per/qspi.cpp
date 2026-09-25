#ifndef UNIT_TEST
#include "per/qspi.h"
#include "sys/system.h"
#include "stm32h7xx_hal.h"
#include "dev/flash_IS25LP080D.h"
#include "dev/flash_IS25LP064A.h"

// TODO: Add handling for alternate device types,
//        This will be a thing much sooner than anticipated
//        due to upgrading the RAM size for the new 4MB chip.
// TODO: AutopollingMemReady only works for 1-Line, not 4-Line

#define RETURN_IF_ERR(func) \
    if(func != Result::OK)  \
        return Result::ERR;

#define ERR_RECOVERY(err)                 \
    SetMode(Config::Mode::MEMORY_MAPPED); \
    status_ = err;                        \
    return Result::ERR;

#define ERR_SIMPLE(err) \
    status_ = err;      \
    return Result::ERR;


namespace daisy
{
/** Private implementation for QSPIHandle */
class QSPIHandle::Impl
{
  public:
    /**
     * extremely minimal constructor just to pre-initialize some flags
     * Everything else is managed through the `Init` function.
     */
    Impl() : init_state_(InitState::Uninitialized), pre_init_complete_(false) {}

    /**
     * Represents the state through which the QSPI peripheral is initialized.
     * An initial single-line initialization should be done once per boot up to ensure
     * the device is accessible, and unintended write protections are disabled.
     */
    enum class InitState
    {
        Uninitialized,
        SingleLineSR,
        Ready,
    };

    QSPIHandle::Result Init(const QSPIHandle::Config& config);

    const QSPIHandle::Config& GetConfig() const { return config_; }

    QSPIHandle::Result DeInit();

    QSPIHandle::Result WritePage(uint32_t address,
                                 uint32_t size,
                                 uint8_t* buffer,
                                 bool     reset_mode = true);

    QSPIHandle::Result Write(uint32_t address, uint32_t size, uint8_t* buffer);

    QSPIHandle::Result Erase(uint32_t start_addr, uint32_t end_addr);

    QSPIHandle::Result EraseSector(uint32_t address);

    QSPIHandle::Result EraseBlock64K(uint32_t address);

    uint32_t GetPin(size_t pin);

    GPIO_TypeDef* GetPort(size_t pin);

    uint8_t GetAF(size_t pin);

    QSPI_HandleTypeDef* GetHalHandle();

    size_t GetNumPins()
    {
        // Uses fewer pins on first pass of initialization
        return init_state_ == InitState::Uninitialized ? 4 : pin_count_;
    }

    Status GetStatus() { return status_; }

    void* GetData(uint32_t offset)
    {
        return (void*)(0x90000000 + (offset & 0x0fffffff));
    }

    InitState GetInitState() const { return init_state_; }

  private:
    QSPIHandle::Result PreInit(uint32_t flash_size);

    QSPIHandle::Result ResetMemory();

    QSPIHandle::Result DummyCyclesConfig(QSPIHandle::Config::Device device);

    QSPIHandle::Result WriteEnable();

    QSPIHandle::Result DefaultStatusRegister();
    /** Reads the status register. Returns false if the read failed. */
    bool ReadStatusRegister(uint8_t* status);

    QSPIHandle::Result QuadEnable();

    QSPIHandle::Result QuadDisable();

    QSPIHandle::Result EnableMemoryMappedMode();

    QSPIHandle::Result AutopollingMemReady(uint32_t timeout);

    QSPIHandle::Result SetMode(Config::Mode mode);

    QSPIHandle::Result CheckProgramMemory();

    // These functions are defined, but we haven't added the ability to switch to quad mode. So they're currently unused.
    QSPIHandle::Result EnterQuadMode() __attribute__((unused));
    QSPIHandle::Result ExitQuadMode() __attribute__((unused));
    uint8_t            GetStatusRegister() __attribute__((unused));

    QSPIHandle::Config config_;
    QSPI_HandleTypeDef halqspi_;
    Status             status_;

    InitState init_state_;
    bool      pre_init_complete_;

    static constexpr size_t pin_count_
        = sizeof(QSPIHandle::Config::pin_config) / sizeof(Pin);
    // Data structure for easy hal initialization
    Pin* pin_config_arr_quad[pin_count_] = {&config_.pin_config.io0,
                                            &config_.pin_config.io1,
                                            &config_.pin_config.io2,
                                            &config_.pin_config.io3,
                                            &config_.pin_config.clk,
                                            &config_.pin_config.ncs};
    Pin* pin_config_arr_sd[4]            = {&config_.pin_config.io0,
                                 &config_.pin_config.io1,
                                 &config_.pin_config.clk,
                                 &config_.pin_config.ncs};
};


// ================================================================
// Global reference to the handle for interfacing with hal
// ================================================================

static QSPIHandle::Impl qspi_impl;

QSPIHandle::Result QSPIHandle::Impl::PreInit(uint32_t flash_size)
{
    // Prior to first pass, set pins for IO2 and IO3 to known HIGH states for duration of first pass, then DeInit:
    const Pin* pre_init_pins[]
        = {&config_.pin_config.io2, &config_.pin_config.io3};

    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitStruct.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
    HAL_Delay(20);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);

    // unnecessarily long delay just to make sure it's visible on the scope prior to init
    HAL_Delay(20);

    halqspi_.Instance                = QUADSPI;
    halqspi_.Init.ClockPrescaler     = 1;
    halqspi_.Init.FifoThreshold      = 1;
    halqspi_.Init.SampleShifting     = QSPI_SAMPLE_SHIFTING_NONE;
    halqspi_.Init.FlashSize          = POSITION_VAL(flash_size) - 1;
    halqspi_.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
    halqspi_.Init.FlashID            = QSPI_FLASH_ID_1;
    halqspi_.Init.DualFlash          = QSPI_DUALFLASH_DISABLE;

    // Round 1 initialization -- configure to single-line,
    // Do Reset, Set StatusReg values to known states, and then de-init
    // to prepare for round 2.
    if(HAL_QSPI_Init(&halqspi_) != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    if(ResetMemory() != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    if(DefaultStatusRegister() != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    if(HAL_QSPI_DeInit(&halqspi_) != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    for(int i = 0; i < 2; i++)
    {
        const Pin*    p = pre_init_pins[i];
        GPIO_TypeDef* port;
        port = GetHALPort(*p);
        HAL_GPIO_DeInit(port, GetHALPin(*p));
    }
    init_state_        = InitState::SingleLineSR;
    pre_init_complete_ = true;
    return QSPIHandle::Result::OK;
}

QSPIHandle::Result QSPIHandle::Impl::Init(const QSPIHandle::Config& config)
{
    RETURN_IF_ERR(CheckProgramMemory());
    // Set Handle Settings     o

    config_     = config;
    auto device = config_.device;
    auto mode   = config_.mode;

    if(HAL_QSPI_DeInit(&halqspi_) != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    init_state_ = pre_init_complete_ ? InitState::SingleLineSR
                                     : InitState::Uninitialized;

    // Set Initialization values for the QSPI Peripheral
    uint32_t flash_size;
    switch(device)
    {
        case QSPIHandle::Config::Device::IS25LP080D:
            flash_size = IS25LP080D_FLASH_SIZE;
            break;
        case QSPIHandle::Config::Device::IS25LP064A:
            flash_size = IS25LP064A_FLASH_SIZE;
            break;
        default: flash_size = IS25LP080D_FLASH_SIZE; break;
    }

    if(init_state_ == InitState::Uninitialized)
        PreInit(flash_size);

    // Round 2 initialization -- all pins will be used.
    halqspi_.Instance                = QUADSPI;
    halqspi_.Init.ClockPrescaler     = 1;
    halqspi_.Init.FifoThreshold      = 1;
    halqspi_.Init.SampleShifting     = QSPI_SAMPLE_SHIFTING_NONE;
    halqspi_.Init.FlashSize          = POSITION_VAL(flash_size) - 1;
    halqspi_.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
    halqspi_.Init.FlashID            = QSPI_FLASH_ID_1;
    halqspi_.Init.DualFlash          = QSPI_DUALFLASH_DISABLE;
    if(HAL_QSPI_Init(&halqspi_) != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    if(ResetMemory() != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    //    uint8_t fifothresh = HAL_QSPI_GetFifoThreshold(&dsy_qspi_handle.hqspi);
    //    uint8_t reg = 0;
    //    reg = get_status_register(&dsy_qspi_handle);
    if(DummyCyclesConfig(device) != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    // Once writing test with 1 Line is confirmed lets move this out, and update writing to use 4-line.
    // This should be redundant, but just in case we want to ensure that the QE is set.
    if(QuadEnable() != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    if(mode == Config::Mode::MEMORY_MAPPED)
    {
        if(EnableMemoryMappedMode() != QSPIHandle::Result::OK)
        {
            ERR_SIMPLE(Status::E_HAL_ERROR);
        }
    }
    init_state_ = InitState::Ready;

    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::DeInit()
{
    halqspi_.Instance = QUADSPI;
    if(HAL_QSPI_DeInit(&halqspi_) != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    // This should actually already get called wihtin the HAL DeInit
    HAL_QSPI_MspDeInit(&halqspi_);
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::WritePage(uint32_t address,
                                               uint32_t size,
                                               uint8_t* buffer,
                                               bool     reset_mode)
{
    RETURN_IF_ERR(CheckProgramMemory());
    RETURN_IF_ERR(SetMode(Config::Mode::INDIRECT_POLLING));

    QSPI_CommandTypeDef s_command;
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = PAGE_PROG_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_1_LINE;
    s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_1_LINE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = size <= 256 ? size : 256;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
    s_command.Address           = address;
    if(WriteEnable() != QSPIHandle::Result::OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }
    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }
    if(HAL_QSPI_Transmit(
           &halqspi_, (uint8_t*)buffer, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }
    if(AutopollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != QSPIHandle::Result::OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }

    if(reset_mode)
        RETURN_IF_ERR(SetMode(Config::Mode::MEMORY_MAPPED));
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result
QSPIHandle::Impl::Write(uint32_t address, uint32_t size, uint8_t* buffer)
{
    uint32_t NumOfPage = 0, NumOfSingle = 0, Addr = 0, count = 0, temp = 0;
    uint32_t QSPI_DataNum    = 0;
    uint32_t flash_page_size = IS25LP080D_PAGE_SIZE;
    address                  = address & 0x0FFFFFFF;
    Addr                     = address % flash_page_size;
    count                    = flash_page_size - Addr;
    NumOfPage                = size / flash_page_size;
    NumOfSingle              = size % flash_page_size;

    if(Addr == 0) /*!< Address is QSPI_PAGESIZE aligned  */
    {
        if(NumOfPage == 0) /*!< NumByteToWrite < QSPI_PAGESIZE */
        {
            QSPI_DataNum = size;
            WritePage(address, QSPI_DataNum, buffer, false);
        }
        else /*!< Size > QSPI_PAGESIZE */
        {
            while(NumOfPage--)
            {
                QSPI_DataNum = flash_page_size;
                WritePage(address, QSPI_DataNum, buffer, false);
                address += flash_page_size;
                buffer += flash_page_size;
            }

            QSPI_DataNum = NumOfSingle;
            if(QSPI_DataNum > 0)
                WritePage(address, QSPI_DataNum, buffer, false);
        }
    }
    else /*!< Address is not QSPI_PAGESIZE aligned  */
    {
        if(NumOfPage == 0) /*!< Size < QSPI_PAGESIZE */
        {
            if(NumOfSingle > count) /*!< (Size + Address) > QSPI_PAGESIZE */
            {
                temp         = NumOfSingle - count;
                QSPI_DataNum = count;
                WritePage(address, QSPI_DataNum, buffer, false);
                address += count;
                buffer += count;
                QSPI_DataNum = temp;
                WritePage(address, QSPI_DataNum, buffer, false);
            }
            else
            {
                QSPI_DataNum = size;
                WritePage(address, QSPI_DataNum, buffer, false);
            }
        }
        else /*!< Size > QSPI_PAGESIZE */
        {
            size -= count;
            NumOfPage    = size / flash_page_size;
            NumOfSingle  = size % flash_page_size;
            QSPI_DataNum = count;
            WritePage(address, QSPI_DataNum, buffer, false);
            address += count;
            buffer += count;

            while(NumOfPage--)
            {
                QSPI_DataNum = flash_page_size;
                WritePage(address, QSPI_DataNum, buffer, false);
                address += flash_page_size;
                buffer += flash_page_size;
            }

            if(NumOfSingle != 0)
            {
                QSPI_DataNum = NumOfSingle;
                WritePage(address, QSPI_DataNum, buffer, false);
            }
        }
    }

    RETURN_IF_ERR(SetMode(Config::Mode::MEMORY_MAPPED));
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::Erase(uint32_t start_addr,
                                           uint32_t end_addr)
{
    uint32_t           block_addr;
    constexpr uint32_t BLOCK_64K = 0x10000; // 64KB
    constexpr uint32_t SECTOR_4K = 0x1000;  // 4KB

    // Align start address down to 4KB boundary
    start_addr = start_addr - (start_addr % SECTOR_4K);

    while(end_addr > start_addr)
    {
        block_addr = start_addr & 0x0FFFFFFF;

        // Use 64KB block erase when aligned and enough space remaining
        if((block_addr % BLOCK_64K) == 0
           && (end_addr - start_addr) >= BLOCK_64K)
        {
            if(EraseBlock64K(block_addr) != QSPIHandle::Result::OK)
            {
                ERR_RECOVERY(Status::E_HAL_ERROR);
            }
            start_addr += BLOCK_64K;
        }
        else
        {
            // Fall back to 4KB sector erase
            if(EraseSector(block_addr) != QSPIHandle::Result::OK)
            {
                ERR_RECOVERY(Status::E_HAL_ERROR);
            }
            start_addr += SECTOR_4K;
        }
    }
    return QSPIHandle::Result::OK;
}

QSPIHandle::Result QSPIHandle::Impl::EraseBlock64K(uint32_t address)
{
    QSPI_CommandTypeDef s_command;
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = BLOCK_ERASE_CMD; // 0xD8 = 64KB block erase
    s_command.AddressMode       = QSPI_ADDRESS_1_LINE;
    s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_NONE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = 1;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
    s_command.Address           = address;

    RETURN_IF_ERR(CheckProgramMemory());
    RETURN_IF_ERR(SetMode(Config::Mode::INDIRECT_POLLING));

    if(WriteEnable() != QSPIHandle::Result::OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }
    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }
    if(AutopollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != QSPIHandle::Result::OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }

    return QSPIHandle::Result::OK;
}

QSPIHandle::Result QSPIHandle::Impl::EraseSector(uint32_t address)
{
    uint8_t             use_qpi = 0;
    QSPI_CommandTypeDef s_command;
    if(use_qpi)
    {
        s_command.InstructionMode = QSPI_INSTRUCTION_4_LINES;
        s_command.Instruction     = SECTOR_ERASE_QPI_CMD;
        s_command.AddressMode     = QSPI_ADDRESS_4_LINES;
    }
    else
    {
        s_command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
        s_command.Instruction     = SECTOR_ERASE_CMD;
        s_command.AddressMode     = QSPI_ADDRESS_1_LINE;
    }
    s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_NONE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = 1;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
    s_command.Address           = address;

    RETURN_IF_ERR(CheckProgramMemory());
    // Erasing takes a long time anyway, so not much point trying to
    // minimize reinitializations
    RETURN_IF_ERR(SetMode(Config::Mode::INDIRECT_POLLING));

    if(WriteEnable() != QSPIHandle::Result::OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }
    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }
    if(AutopollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != QSPIHandle::Result::OK)
    {
        ERR_RECOVERY(Status::E_HAL_ERROR);
    }

    RETURN_IF_ERR(SetMode(Config::Mode::MEMORY_MAPPED));
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::ResetMemory()
{
    QSPI_CommandTypeDef s_command;

    /* Initialize the reset enable command */
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = RESET_ENABLE_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_NONE;
    s_command.DummyCycles       = 0;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    /* Send the command */
    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    /* Send the reset memory command */
    s_command.Instruction = RESET_MEMORY_CMD;
    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    /* Configure automatic polling mode to wait the memory is ready */
    if(AutopollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result
QSPIHandle::Impl::DummyCyclesConfig(QSPIHandle::Config::Device device)
{
    QSPI_CommandTypeDef s_command;
    uint16_t            reg     = 0;
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_1_LINE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = 1;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
    if(device == QSPIHandle::Config::Device::IS25LP080D)
    {
        /* Initialize the read volatile configuration register command */
        s_command.Instruction = READ_READ_PARAM_REG_CMD;

        /* Configure the command */
        if(HAL_QSPI_Command(
               &halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
           != HAL_OK)
        {
            ERR_SIMPLE(Status::E_HAL_ERROR);
        }

        /* Reception of the data */
        if(HAL_QSPI_Receive(
               &halqspi_, (uint8_t*)(&reg), HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
           != HAL_OK)
        {
            ERR_SIMPLE(Status::E_HAL_ERROR);
        }
        MODIFY_REG(reg, 0x78, (IS25LP080D_DUMMY_CYCLES_READ_QUAD << 3));
        /* Enable write operations */
        if(WriteEnable() != QSPIHandle::Result::OK)
        {
            ERR_SIMPLE(Status::E_HAL_ERROR);
        }
    }
    else
    {
        // Only volatile Read Params on 16MB chip.
        // Explicitly set:
        // Burst Length: 8 bytes (0, 0)
        // Wrap Enable: 0
        // Dummy Cycles: (Config 3, bits 1 0)
        // Drive Strength (50%, bits 1 1 1)
        // Byte to write: 0b11110000 (0xF0)
        // TODO: Probably expand Burst to maximum if that works out.

        reg = 0xF0;
    }


    /* Update volatile configuration register (with new dummy cycles) */
    s_command.Instruction = WRITE_READ_PARAM_REG_CMD;

    /* Configure the write volatile configuration register command */
    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    /* Transmission of the data */
    if(HAL_QSPI_Transmit(
           &halqspi_, (uint8_t*)(&reg), HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    /* Configure automatic polling mode to wait the memory is ready */
    if(AutopollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::WriteEnable()
{
    QSPI_CommandTypeDef     s_command;
    QSPI_AutoPollingTypeDef s_config;

    /* Enable write operations */
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = WRITE_ENABLE_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_NONE;
    s_command.DummyCycles       = 0;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    RETURN_IF_ERR(CheckProgramMemory());

    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    /* Configure automatic polling mode to wait for write enabling */
    //        s_config.Match           = IS25LP080D_SR_WREN | (IS25LP080D_SR_WREN << 8);
    //        s_config.Mask            = IS25LP080D_SR_WREN | (IS25LP080D_SR_WREN << 8);
    s_config.MatchMode       = QSPI_MATCH_MODE_AND;
    s_config.Match           = IS25LP080D_SR_WREN;
    s_config.Mask            = IS25LP080D_SR_WREN;
    s_config.Interval        = 0x10;
    s_config.StatusBytesSize = 1;
    s_config.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

    s_command.Instruction = READ_STATUS_REG_CMD;
    s_command.DataMode    = QSPI_DATA_1_LINE;

    if(HAL_QSPI_AutoPolling(
           &halqspi_, &s_command, &s_config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::QuadEnable()
{
    QSPI_CommandTypeDef     s_command;
    QSPI_AutoPollingTypeDef s_config;
    uint8_t                 reg = 0;

    /* QE is a non-volatile bit, and Init (i.e. every switch between indirect
       and memory-mapped mode) calls this each time. Writing the status
       register wears it and can take a long time on a worn part, so skip the
       write when the register is already in the state the write below polls
       for: QE set, no write in progress, write-enable latch clear. Anything
       else, including a failed read, falls through to the write. */
    {
        uint8_t status = 0;
        if(ReadStatusRegister(&status) && (status & IS25LP080D_SR_QE)
           && !(status & (IS25LP080D_SR_WIP | IS25LP080D_SR_WREN)))
            return QSPIHandle::Result::OK;
    }

    /* Enable write operations */
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = WRITE_STATUS_REG_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_1_LINE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = 1;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    /* Enable write operations */
    if(WriteEnable() != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }


    //    reg = 0;
    //    MODIFY_REG(reg,
    //        0xF0,
    //        (IS25LP08D_SR_QE));
    reg = IS25LP080D_SR_QE; // Set QE bit  to 1
    /* Transmission of the data */
    if(HAL_QSPI_Transmit(
           &halqspi_, (uint8_t*)(&reg), HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    /* Configure automatic polling mode to wait for write enabling */
    //    s_config.Match           = IS25LP08D_SR_WREN | (IS25LP08D_SR_WREN << 8);
    //    s_config.Mask            = IS25LP08D_SR_WREN | (IS25LP08D_SR_WREN << 8);
    //    s_config.MatchMode       = QSPI_MATCH_MODE_AND;
    //    s_config.StatusBytesSize = 2;

    // Poll until QE is set, and WREN and WIP are reset.
    s_config.Match = IS25LP080D_SR_QE;
    s_config.Mask  = IS25LP080D_SR_QE | IS25LP080D_SR_WREN | IS25LP080D_SR_WIP;
    s_config.MatchMode       = QSPI_MATCH_MODE_AND;
    s_config.StatusBytesSize = 1;

    //s_config.Interval      = 0x10;
    s_config.Interval      = 0x8000;
    s_config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

    s_command.Instruction = READ_STATUS_REG_CMD;
    s_command.DataMode    = QSPI_DATA_1_LINE;

    if(HAL_QSPI_AutoPolling(
           &halqspi_, &s_command, &s_config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    /* Configure automatic polling mode to wait the memory is ready */
    if(AutopollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    return QSPIHandle::Result::OK;
}

bool QSPIHandle::Impl::ReadStatusRegister(uint8_t* status)
{
    QSPI_CommandTypeDef r_command;
    r_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    r_command.Instruction       = READ_STATUS_REG_CMD;
    r_command.AddressMode       = QSPI_ADDRESS_NONE;
    r_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    r_command.DataMode          = QSPI_DATA_1_LINE;
    r_command.DummyCycles       = 0;
    r_command.NbData            = 1;
    r_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    r_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    r_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
    return HAL_QSPI_Command(
               &halqspi_, &r_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
               == HAL_OK
           && HAL_QSPI_Receive(
                  &halqspi_, status, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
                  == HAL_OK;
}

QSPIHandle::Result QSPIHandle::Impl::DefaultStatusRegister()
{
    /* The default below (0x40: QE set, no block protection, SRWD clear) is
       non-volatile, so skip rewriting it on every boot when the register
       already holds exactly that value. Any other value (e.g. SRWD or block
       protection set) is still rewritten. The read happens before WREN/WRSR
       are issued so it cannot split the write command from its data. */
    {
        uint8_t status = 0;
        if(ReadStatusRegister(&status) && status == 0x40)
            return QSPIHandle::Result::OK;
    }
    QSPI_CommandTypeDef     s_command;
    QSPI_AutoPollingTypeDef s_config;

    /* Enable write operations */
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = WRITE_STATUS_REG_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_1_LINE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = 1;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    /* Enable write operations */
    if(WriteEnable() != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    // Default status register chunk:
    // SRWD (Status Register Write Disable) = 0
    // QE (Quad Enable) = 0
    // BP3, BP2, BP1, BP0 (Block Protect) = 0
    // WEL/WIP are volatile bits, so we don't need to set/reset them here
    //uint8_t reg = 0x00;
    // We'll set QE here so that the WP/HOLD functions are disbaled.
    uint8_t reg = 0x40;

    /* Transmission of the data */
    if(HAL_QSPI_Transmit(
           &halqspi_, (uint8_t*)(&reg), HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    s_config.Match = reg;
    // Autopolling complete when status register is completely back to defaults.
    s_config.Mask            = 0xff;
    s_config.MatchMode       = QSPI_MATCH_MODE_AND;
    s_config.StatusBytesSize = 1;

    // write status register takes approx 2-15ms to complete according to datasheet.
    // interval is in clock cycles so at 120MHz, we'll check every 250us or so instead of
    // the original 133ns
    s_config.Interval      = 0x8000;
    s_config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

    s_command.Instruction = READ_STATUS_REG_CMD;
    s_command.DataMode    = QSPI_DATA_1_LINE;

    if(HAL_QSPI_AutoPolling(
           &halqspi_, &s_command, &s_config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }

    /* Configure automatic polling mode to wait the memory is ready */
    if(AutopollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::EnableMemoryMappedMode()
{
    QSPI_CommandTypeDef      s_command;
    QSPI_MemoryMappedTypeDef s_mem_mapped_cfg;

    /* Configure the command for the read instruction */
    s_command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction     = QUAD_INOUT_FAST_READ_CMD;
    s_command.AddressMode     = QSPI_ADDRESS_4_LINES;
    s_command.AddressSize     = QSPI_ADDRESS_24_BITS;
    //    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    //s_command.DummyCycles       = IS25LP080D_DUMMY_CYCLES_READ_QUAD;
    s_command.AlternateByteMode  = QSPI_ALTERNATE_BYTES_4_LINES;
    s_command.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    s_command.AlternateBytes     = 0x000000A0;
    s_command.DummyCycles        = 6;
    s_command.DdrMode            = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle   = QSPI_DDR_HHC_ANALOG_DELAY;
    //s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
    s_command.SIOOMode = QSPI_SIOO_INST_ONLY_FIRST_CMD;
    s_command.DataMode = QSPI_DATA_4_LINES;

    /* Configure the memory mapped mode */
    s_mem_mapped_cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    s_mem_mapped_cfg.TimeOutPeriod     = 0;

    if(HAL_QSPI_MemoryMapped(&halqspi_, &s_command, &s_mem_mapped_cfg)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::AutopollingMemReady(uint32_t timeout)
{
    QSPI_CommandTypeDef     s_command;
    QSPI_AutoPollingTypeDef s_config;

    /* Configure automatic polling mode to wait for memory ready */
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = READ_STATUS_REG_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_1_LINE;
    s_command.DummyCycles       = 0;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    s_config.Match         = 0;
    s_config.MatchMode     = QSPI_MATCH_MODE_AND;
    s_config.Interval      = 0x10;
    s_config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;
    s_config.Mask          = IS25LP080D_SR_WIP;
    //s_config.Mask            = 0;
    s_config.StatusBytesSize = 1;

    if(HAL_QSPI_AutoPolling(&halqspi_, &s_command, &s_config, timeout)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    return QSPIHandle::Result::OK;
}

QSPIHandle::Result QSPIHandle::Impl::SetMode(QSPIHandle::Config::Mode mode)
{
    if(config_.mode != mode)
    {
        config_.mode = mode;
        if(Init(config_) != Result::OK)
        {
            config_.mode = Config::Mode::MEMORY_MAPPED;
            status_      = Status::E_SWITCHING_MODES;
            Init(config_);
            return Result::ERR;
        }
    }
    return Result::OK;
}

QSPIHandle::Result QSPIHandle::Impl::CheckProgramMemory()
{
    if(System::GetProgramMemoryRegion() == System::MemoryRegion::QSPI)
    {
        status_ = Status::E_INVALID_MODE;
        // SetMode(Config::Mode::MEMORY_MAPPED);
        return Result::ERR;
    }
    return Result::OK;
}

QSPIHandle::Result QSPIHandle::Impl::EnterQuadMode()
{
    QSPI_CommandTypeDef s_command;

    /* Initialize the read volatile configuration register command */
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = ENTER_QUAD_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_NONE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = 1;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    /* Configure the command */
    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    //    /* Wait for WIP bit in SR */
    //    if (QSPI_AutoPollingMemReady(&halqspi_, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != QSPIHandle::Result::OK)
    //    {
    //        return QSPIHandle::Result::ERR;
    //    }
    return QSPIHandle::Result::OK;
}


QSPIHandle::Result QSPIHandle::Impl::ExitQuadMode()
{
    QSPI_CommandTypeDef s_command;

    /* Initialize the read volatile configuration register command */
    s_command.InstructionMode   = QSPI_INSTRUCTION_4_LINES;
    s_command.Instruction       = EXIT_QUAD_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_NONE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = 1;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    /* Configure the command */
    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    /* Wait for WIP bit in SR */
    if(AutopollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != QSPIHandle::Result::OK)
    {
        ERR_SIMPLE(Status::E_HAL_ERROR);
    }
    return QSPIHandle::Result::OK;
}


uint8_t QSPIHandle::Impl::GetStatusRegister()
{
    QSPI_CommandTypeDef s_command;
    uint8_t             reg;
    reg                         = 0x00;
    s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    s_command.Instruction       = READ_STATUS_REG_CMD;
    s_command.AddressMode       = QSPI_ADDRESS_NONE;
    s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    s_command.DataMode          = QSPI_DATA_1_LINE;
    s_command.DummyCycles       = 0;
    s_command.NbData            = 1;
    s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
    s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    if(HAL_QSPI_Command(&halqspi_, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        return 0x00;
    }
    if(HAL_QSPI_Receive(
           &halqspi_, (uint8_t*)(&reg), HAL_QPSI_TIMEOUT_DEFAULT_VALUE)
       != HAL_OK)
    {
        return 0x00;
    }
    return reg;
}


uint32_t QSPIHandle::Impl::GetPin(size_t pin)
{
    Pin* p;
    if(init_state_ == InitState::Uninitialized)
        p = pin_config_arr_sd[pin];
    else
        p = pin_config_arr_quad[pin];
    return GetHALPin(*p);
}


GPIO_TypeDef* QSPIHandle::Impl::GetPort(size_t pin)
{
    Pin* p;
    if(init_state_ == InitState::Uninitialized)
        p = pin_config_arr_sd[pin];
    else
        p = pin_config_arr_quad[pin];
    return GetHALPort(*p);
}

uint8_t QSPIHandle::Impl::GetAF(size_t pin)
{
    if(init_state_ == InitState::Uninitialized)
    {
        uint8_t af_config[] = {GPIO_AF10_QUADSPI,
                               GPIO_AF10_QUADSPI,
                               GPIO_AF9_QUADSPI,
                               GPIO_AF10_QUADSPI};
        return af_config[pin];
    }
    // otherwise use all pins
    uint8_t af_config[] = {GPIO_AF10_QUADSPI,
                           GPIO_AF10_QUADSPI,
                           GPIO_AF9_QUADSPI,
                           GPIO_AF9_QUADSPI,
                           GPIO_AF9_QUADSPI,
                           GPIO_AF10_QUADSPI};
    return af_config[pin];
}


QSPI_HandleTypeDef* QSPIHandle::Impl::GetHalHandle()
{
    return &halqspi_;
}


// ======================================================================
// QSPIHandle > QSPIHandle::Impl
// ======================================================================

QSPIHandle::Result QSPIHandle::Init(const QSPIHandle::Config& config)
{
    pimpl_ = &qspi_impl;
    return pimpl_->Init(config);
}

const QSPIHandle::Config& QSPIHandle::GetConfig() const
{
    return pimpl_->GetConfig();
}

QSPIHandle::Result QSPIHandle::DeInit()
{
    return pimpl_->DeInit();
}

QSPIHandle::Status QSPIHandle::GetStatus()
{
    return pimpl_->GetStatus();
}

QSPIHandle::Result
QSPIHandle::WritePage(uint32_t address, uint32_t size, uint8_t* buffer)
{
    return pimpl_->WritePage(address, size, buffer);
}

QSPIHandle::Result
QSPIHandle::Write(uint32_t address, uint32_t size, uint8_t* buffer)
{
    return pimpl_->Write(address, size, buffer);
}

QSPIHandle::Result QSPIHandle::Erase(uint32_t start_addr, uint32_t end_addr)
{
    return pimpl_->Erase(start_addr, end_addr);
}

QSPIHandle::Result QSPIHandle::EraseSector(uint32_t address)
{
    return pimpl_->EraseSector(address);
}

void* QSPIHandle::GetData(uint32_t offset)
{
    return pimpl_->GetData(offset);
}

// ======================================================================
// HAL service functions
// ======================================================================

extern "C" void HAL_QSPI_MspInit(QSPI_HandleTypeDef* qspiHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(qspiHandle->Instance == QUADSPI)
    {
        /* QUADSPI clock enable */
        __HAL_RCC_QSPI_CLK_ENABLE();

        /** Reset/Release (as per second instruction in How to use this driver) */
        __HAL_RCC_QSPI_FORCE_RESET();
        __HAL_RCC_QSPI_RELEASE_RESET();

        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_GPIOF_CLK_ENABLE();
        __HAL_RCC_GPIOE_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        // Seems the same for all pin outs so far.
        GPIO_TypeDef* port;
        for(uint8_t i = 0; i < qspi_impl.GetNumPins(); i++)
        {
            port                      = qspi_impl.GetPort(i);
            GPIO_InitStruct.Pin       = qspi_impl.GetPin(i);
            GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
            GPIO_InitStruct.Pull      = GPIO_NOPULL;
            GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
            GPIO_InitStruct.Alternate = qspi_impl.GetAF(i);
            HAL_GPIO_Init(port, &GPIO_InitStruct);
        }
        /* QUADSPI interrupt Init */
        HAL_NVIC_SetPriority(QUADSPI_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(QUADSPI_IRQn);
    }
}

extern "C" void HAL_QSPI_MspDeInit(QSPI_HandleTypeDef* qspiHandle)
{
    if(qspiHandle->Instance == QUADSPI)
    {
        /* Peripheral clock disable */
        __HAL_RCC_QSPI_CLK_DISABLE();
        GPIO_TypeDef* port;
        uint16_t      pin;
        for(uint8_t i = 0; i < qspi_impl.GetNumPins(); i++)
        {
            //            port = (GPIO_TypeDef *)
            //                gpio_hal_port_map[qspi_handle.dsy_hqspi->pin_config[i].port];
            //            pin = gpio_hal_pin_map[qspi_handle.dsy_hqspi->pin_config[i].pin];
            port = qspi_impl.GetPort(i);
            pin  = qspi_impl.GetPin(i);
            HAL_GPIO_DeInit(port, pin);
        }
        /* QUADSPI interrupt Deinit */
        HAL_NVIC_DisableIRQ(QUADSPI_IRQn);
        /* USER CODE BEGIN QUADSPI_MspDeInit 1 */

        /* USER CODE END QUADSPI_MspDeInit 1 */
    }
}

extern "C" void QUADSPI_IRQHandler(void)
{
    HAL_QSPI_IRQHandler(qspi_impl.GetHalHandle());
}

} // namespace daisy

/* HAL Overwrite Implementation */

/**QUADSPI GPIO Configuration
    On Daisy Rev3:
    PG6     ------> QUADSPI_BK1_NCS
    PF8     ------> QUADSPI_BK1_IO0
    PF9     ------> QUADSPI_BK1_IO1
    PF7     ------> QUADSPI_BK1_IO2
    PF6     ------> QUADSPI_BK1_IO3
    PB2     ------> QUADSPI_CLK
    On Daisy Seed:
    PG6     ------> QUADSPI_BK1_NCS
    PF8     ------> QUADSPI_BK1_IO0
    PF9     ------> QUADSPI_BK1_IO1
    PF7     ------> QUADSPI_BK1_IO2
    PF6     ------> QUADSPI_BK1_IO3
    PF10    ------> QUADSPI_CLK
    On Audio BB:
    PG6     ------> QUADSPI_BK1_NCS
    PF8     ------> QUADSPI_BK1_IO0
    PF9     ------> QUADSPI_BK1_IO1
    PE2     ------> QUADSPI_BK1_IO2
    PF6     ------> QUADSPI_BK1_IO3
    PF10    ------> QUADSPI_CLK
    */
//enum
//{
//    DSY_QSPI_AF_PINS_NCS,
//    DSY_QSPI_AF_PINS_IO0,
//    DSY_QSPI_AF_PINS_IO1,
//    DSY_QSPI_AF_PINS_IO2,
//    DSY_QSPI_AF_PINS_IO3,
//    DSY_QSPI_AF_PINS_CLK,
//    DSY_QSPI_AF_PINS_LAST
//};
//static GPIO_TypeDef *gpio_config_ports[DSY_SYS_BOARD_LAST][DSY_QSPI_AF_PINS_LAST] = {
//    // NCS, IO0,   IO1,   IO2,   IO3,   CLK
//    {GPIOG, GPIOF, GPIOF, GPIOF, GPIOF, GPIOB}, // DAISY
//    {GPIOG, GPIOF, GPIOF, GPIOF, GPIOF, GPIOF}, // DAISY SEED
//    {GPIOG, GPIOF, GPIOF, GPIOE, GPIOF, GPIOF}, // AUDIO BB
//};
//static uint16_t gpio_config_pins[DSY_SYS_BOARD_LAST][DSY_QSPI_AF_PINS_LAST] = {
//    // NCS,         IO0,         IO1,         IO2,         IO3,         CLK
//    {GPIO_PIN_6, GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_7, GPIO_PIN_6, GPIO_PIN_2}, // DAISY
//    {GPIO_PIN_6, GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_7, GPIO_PIN_6, GPIO_PIN_10}, // DAISY SEED
//    {GPIO_PIN_6, GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_2, GPIO_PIN_6, GPIO_PIN_10}, // AUDIO BB
//};
//
//static uint8_t gpio_config_af[DSY_SYS_BOARD_LAST][DSY_QSPI_AF_PINS_LAST] = {
//    // NCS,                IO0,                IO1,                IO2,                IO3,           CLK
//    {GPIO_AF10_QUADSPI, GPIO_AF10_QUADSPI, GPIO_AF10_QUADSPI, GPIO_AF9_QUADSPI, GPIO_AF9_QUADSPI, GPIO_AF9_QUADSPI}, // DAISY
//    {GPIO_AF10_QUADSPI, GPIO_AF10_QUADSPI, GPIO_AF10_QUADSPI, GPIO_AF9_QUADSPI, GPIO_AF9_QUADSPI, GPIO_AF9_QUADSPI}, // DAISY SEED
//    {GPIO_AF10_QUADSPI, GPIO_AF10_QUADSPI, GPIO_AF10_QUADSPI, GPIO_AF9_QUADSPI, GPIO_AF9_QUADSPI, GPIO_AF9_QUADSPI}, // AUDIO BB
//};
//

#else // ifndef UNIT_TEST

#include "qspi.h"
// static isolator for the dummy version used in unit tests
TestIsolator<daisy::QSPIHandle::QSPIState> daisy::QSPIHandle::testIsolator_;

#endif
