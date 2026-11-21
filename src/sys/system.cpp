#ifndef UNIT_TEST // for unit tests, a dummy implementation is provided below
#include <stm32h7xx_hal.h>
#include <stm32h750xx.h>
#include "sys/system.h"
#include "sys/dma.h"
#include "per/gpio.h"
#include "per/rng.h"

// global init functions for peripheral drivers.
// These don't really need to be extern "C" anymore..
extern "C"
{
    extern void dsy_i2c_global_init();
    extern void dsy_spi_global_init();
    extern void dsy_uart_global_init();
}

// boot info struct declared in persistent backup SRAM
volatile daisy::System::BootInfo __attribute__((section(".backup_sram")))
daisy::boot_info;

// Jump related stuff
#define u32 uint32_t
#define vu32 volatile uint32_t
#define SET_REG(addr, val)    \
    do                        \
    {                         \
        *(vu32*)(addr) = val; \
    } while(0)

#define GET_REG(addr) (*(vu32*)(addr))
#define RCC_CR RCC
#define RCC_CFGR (RCC + 0x04)
#define RCC_CIR (RCC + 0x08)
#define RCC_AHBENR (RCC + 0x14)
#define RCC_APB2ENR (RCC + 0x18)
#define RCC_APB1ENR (RCC + 0x1C)

#define SCS 0xE000E000
#define STK (SCS + 0x10)
#define STK_CTRL (STK + 0x00)
#define RCC_CR RCC

#define SDRAM_BASE 0xC0000000

#define INTERNAL_FLASH_SIZE 0x20000
#define ITCMRAM_SIZE 0x10000
#define DTCMRAM_SIZE 0x20000
#define SRAM_D1_SIZE 0x80000
#define SRAM_D2_SIZE 0x48000
#define SRAM_D3_SIZE 0x10000
#define SDRAM_SIZE 0x4000000
#define QSPI_SIZE 0x800000

typedef struct
{
    vu32 ISER[2];
    u32  RESERVED0[30];
    vu32 ICER[2];
    u32  RSERVED1[30];
    vu32 ISPR[2];
    u32  RESERVED2[30];
    vu32 ICPR[2];
    u32  RESERVED3[30];
    vu32 IABR[2];
    u32  RESERVED4[62];
    vu32 IPR[15];
} NVIC_TypeDef;

__attribute__((always_inline)) static inline void __JUMPTOQSPI()
{
    __asm("LDR R1, =0xE000ED00;"); // SCB
    __asm("LDR R0, =0x90000000;"); // APP BASE
    __asm("STR R0, [R1, #8]");     // VTOR
    __asm("LDR SP, [R0, #0]");     // SP @ +0
    __asm("LDR R0, [R0, #4]");     // PC @ +4
    __asm("BX R0");
}

typedef void (*EntryPoint)(void);

// Static Function Declaration
//static void SystemClock_Config();
//static void MPU_Config();
static void Error_Handler(void);

// System Level C functions and IRQ Handlers
extern "C"
{
    void SysTick_Handler(void)
    {
        HAL_IncTick();
        HAL_SYSTICK_IRQHandler();
    }

    /** USB IRQ Handlers since they are shared resources for multiple classes */
    extern HCD_HandleTypeDef hhcd_USB_OTG_HS;
    extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

    void OTG_HS_EP1_OUT_IRQHandler(void)
    {
        if(hhcd_USB_OTG_HS.Instance)
            HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS);
        if(hpcd_USB_OTG_HS.Instance)
            HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
    }

    void OTG_HS_EP1_IN_IRQHandler(void)
    {
        if(hhcd_USB_OTG_HS.Instance)
            HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS);
        if(hpcd_USB_OTG_HS.Instance)
            HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
    }

    void OTG_HS_IRQHandler(void)
    {
        if(hhcd_USB_OTG_HS.Instance)
            HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS);
        if(hpcd_USB_OTG_HS.Instance)
            HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
    }

    // TODO: Add some real handling to the HardFaultHandler
    void HardFault_Handler()
    {
        // Grab an instance of the SCB so we can `p/x *scb` from the debugger
        SCB_Type* scb = SCB;
        (void)(scb);

        // Identify hardfault type
        if(SCB->HFSR & SCB_HFSR_FORCED_Msk)
        {
            // Forced hardfault

            // Copy faults for easy debugging
            size_t mmu_fault = (SCB->CFSR >> 0) & 0xFF;
            (void)(mmu_fault);
            size_t bus_fault = (SCB->CFSR >> 8) & 0xFF;
            (void)(bus_fault);
            size_t usage_fault = (SCB->CFSR >> 16) & 0xFFFF;
            (void)(usage_fault);

            // Check for memory manger faults
            if(SCB->CFSR & SCB_CFSR_MMARVALID_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_MLSPERR_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_MSTKERR_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_MUNSTKERR_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_DACCVIOL_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_IACCVIOL_Msk)
                __asm("BKPT #0");

            // Check for bus faults
            if(SCB->CFSR & SCB_CFSR_BFARVALID_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_LSPERR_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_STKERR_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_UNSTKERR_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_IMPRECISERR_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_PRECISERR_Msk)
                __asm("BKPT #0");
            if(SCB->CFSR & SCB_CFSR_IBUSERR_Msk)
                __asm("BKPT #0");

            // Check for usage faults
            if(SCB->CFSR & SCB_CFSR_DIVBYZERO_Msk)
                __asm("BKPT 0");
            if(SCB->CFSR & SCB_CFSR_UNALIGNED_Msk)
                __asm("BKPT 0");
            if(SCB->CFSR & SCB_CFSR_NOCP_Msk)
                __asm("BKPT 0");
            if(SCB->CFSR & SCB_CFSR_INVPC_Msk)
                __asm("BKPT 0");
            if(SCB->CFSR & SCB_CFSR_INVSTATE_Msk)
                __asm("BKPT 0");
            if(SCB->CFSR & SCB_CFSR_UNDEFINSTR_Msk)
                __asm("BKPT 0");

            __asm("BKPT #0");
        }
        else if(SCB->HFSR & SCB_HFSR_VECTTBL_Msk)
        {
            // Vector table bus fault

            __asm("BKPT #0");
        }

        __asm("BKPT #0");
        while(1)
            ;
    }
}

namespace daisy
{
// Define static tim_
TimerHandle System::tim_;

void System::Init()
{
    System::Config cfg;
    cfg.Defaults();
    Init(cfg);
}

// TODO: DONT FORGET TO IMPLEMENT CLOCK CONFIG
void System::Init(const System::Config& config)
{
    cfg_ = config;
    HAL_Init();
    if(!config.skip_clocks)
    {
        ConfigureClocks();
        ConfigureMpu();
    }
    dsy_dma_init();
    dsy_i2c_global_init();
    dsy_spi_global_init();
    dsy_uart_global_init();

    // Initialize Caches
    if(config.use_dcache)
        SCB_EnableDCache();
    if(config.use_icache)
        SCB_EnableICache();

    // Configure and start highspeed timer.
    // TIM 2 counter UP (defaults to fastest tick/longest period).
    TimerHandle::Config timcfg;
    timcfg.periph = TimerHandle::Config::Peripheral::TIM_2;
    timcfg.dir    = TimerHandle::Config::CounterDir::UP;
    tim_.Init(timcfg);
    tim_.Start();

    // Initialize the true random number generator
    Random::Init();
}

void System::DeInit()
{
    dsy_dma_deinit();
    // HAL_MPU_Disable();
    // The I2C global init doesn't actually initialize the periph,
    // so no need to deinitialize
    SCB_DisableDCache();
    SCB_DisableICache();

    tim_.DeInit();
    // HAL_RCC_DeInit();

    // WARNING -- without modifications, this function will
    // cause the device to reset, preventing program loading
    // HAL_DeInit();
}

void System::JumpToQspi()
{
    __JUMPTOQSPI();
    while(1) {}
}

uint32_t System::GetNow()
{
    return HAL_GetTick();
}

uint32_t System::GetUs()
{
    return tim_.GetUs();
}

uint32_t System::GetTick()
{
    return tim_.GetTick();
}

void System::Delay(uint32_t delay_ms)
{
    HAL_Delay(delay_ms);
}

void System::DelayUs(uint32_t delay_us)
{
    tim_.DelayUs(delay_us);
}

void System::DelayTicks(uint32_t delay_ticks)
{
    tim_.DelayTick(delay_ticks);
}

void System::ResetToBootloader(BootloaderMode mode)
{
    if(mode == BootloaderMode::STM)
    {
        // Initialize Boot Pin
        constexpr Pin bootpin = Pin(PORTG, 3);

        GPIO pin;
        pin.Init(bootpin, GPIO::Mode::OUTPUT);

        // Pull Pin HIGH
        pin.Write(1);

        // wait a few ms for cap to charge
        HAL_Delay(10);
    }
    else if(mode == BootloaderMode::DAISY
            || mode == BootloaderMode::DAISY_SKIP_TIMEOUT
            || mode == BootloaderMode::DAISY_INFINITE_TIMEOUT)
    {
        auto region = GetProgramMemoryRegion();
        if(region == MemoryRegion::INTERNAL_FLASH)
            return; // Cannot return to Daisy bootloader if it's not present!

        // Coming from a bootloaded program, the backup SRAM will already
        // be initialized. If the bootloader is <= v5, then it will not
        // be initialized, but a failed write will not cause a fault.
        switch(mode)
        {
            case BootloaderMode::DAISY_SKIP_TIMEOUT:
                boot_info.status = BootInfo::Type::SKIP_TIMEOUT;
                break;
            case BootloaderMode::DAISY_INFINITE_TIMEOUT:
                boot_info.status = BootInfo::Type::INF_TIMEOUT;
                break;
            default:
                // this is technically valid, just means no
                // special behavior applied on boot
                boot_info.status = BootInfo::Type::INVALID;
                break;
        }
    }
    else
    {
        return; // Malformed mode
    }

    // disable interupts
    RCC->CIER = 0x00000000;

    // Software Reset
    HAL_NVIC_SystemReset();
}

void System::InitBackupSram()
{
    PWR->CR1 |= PWR_CR1_DBP;
    while((PWR->CR1 & PWR_CR1_DBP) == RESET)
        ;
    __HAL_RCC_BKPRAM_CLK_ENABLE();
}

System::BootInfo::Version System::GetBootloaderVersion()
{
    auto region = GetProgramMemoryRegion();
    if(region == MemoryRegion::INTERNAL_FLASH)
        return BootInfo::Version::NONE;

    for(int i = 0; i < (int)BootInfo::Version::LAST; i++)
    {
        if(boot_info.version == (BootInfo::Version)i)
            return (BootInfo::Version)i;
    }

    // Otherwise, the version may be higher than this
    // version of the library knows about. In that case,
    // expecting backwards compatibility, we'll say it's
    // at least the latest version.
    return (BootInfo::Version)((int)BootInfo::Version::LAST - 1);
}

void System::ConfigureClocks()
{
    RCC_OscInitTypeDef       RCC_OscInitStruct   = {0};
    RCC_ClkInitTypeDef       RCC_ClkInitStruct   = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /** Supply configuration update enable
  */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /** Configure the main internal regulator output voltage
     ** and set PLLN value, and flash-latency.
     **
     ** See page 159 of Reference manual for VOS/Freq relationship
     ** and table for flash latency.
     */

    uint32_t plln_val, flash_latency;
    switch(cfg_.cpu_freq)
    {
        case Config::SysClkFreq::FREQ_480MHZ:
            __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
            plln_val      = 240;
            flash_latency = FLASH_LATENCY_4;
            break;
        case Config::SysClkFreq::FREQ_400MHZ:
        default:
            __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
            plln_val      = 200;
            flash_latency = FLASH_LATENCY_2;
            break;
    }

    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
    /** Macro to configure the PLL clock source
  */
    __HAL_RCC_PLL_PLLSOURCE_CONFIG(RCC_PLLSOURCE_HSE);
    /** Initializes the CPU, AHB and APB busses clocks
  */
    RCC_OscInitStruct.OscillatorType
        = RCC_OSCILLATORTYPE_HSI48 | RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState      = RCC_HSE_ON;
    RCC_OscInitStruct.HSI48State    = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState  = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM      = 4;
    //
    RCC_OscInitStruct.PLL.PLLN      = plln_val;
    RCC_OscInitStruct.PLL.PLLP      = 2;
    RCC_OscInitStruct.PLL.PLLQ      = 5; // was 4 in cube
    RCC_OscInitStruct.PLL.PLLR      = 2;
    RCC_OscInitStruct.PLL.PLLRGE    = RCC_PLL1VCIRANGE_2;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN  = 0;
    if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
    /** Initializes the CPU, AHB and APB busses clocks
  */
    RCC_ClkInitStruct.ClockType
        = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1
          | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, flash_latency) != HAL_OK)
    {
        Error_Handler();
    }
    PeriphClkInitStruct.PeriphClockSelection
        = RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_USART6
          | RCC_PERIPHCLK_USART234578 | RCC_PERIPHCLK_LPUART1
          | RCC_PERIPHCLK_RNG | RCC_PERIPHCLK_SPI1 | RCC_PERIPHCLK_SPI6
          | RCC_PERIPHCLK_SAI2 | RCC_PERIPHCLK_SAI1 | RCC_PERIPHCLK_SDMMC
          | RCC_PERIPHCLK_I2C2 | RCC_PERIPHCLK_ADC | RCC_PERIPHCLK_I2C1
          | RCC_PERIPHCLK_USB | RCC_PERIPHCLK_QSPI | RCC_PERIPHCLK_FMC;
    // PLL 2
    //  PeriphClkInitStruct.PLL2.PLL2N = 115; // Max Freq @ 3v3
    //PeriphClkInitStruct.PLL2.PLL2N      = 84; // Max Freq @ 1V9
    /** Old Timing  */
    /*
    PeriphClkInitStruct.PLL2.PLL2N      = 100; // Max supported freq of FMC;
    PeriphClkInitStruct.PLL2.PLL2M      = 4;
    PeriphClkInitStruct.PLL2.PLL2P      = 8;  // 57.5
    PeriphClkInitStruct.PLL2.PLL2Q      = 10; // 46
    PeriphClkInitStruct.PLL2.PLL2R      = 2;  // 115Mhz
    PeriphClkInitStruct.PLL2.PLL2FRACN  = 0;
    */

    // New Timing
    PeriphClkInitStruct.PLL2.PLL2N     = 12; // Max supported freq of FMC;
    PeriphClkInitStruct.PLL2.PLL2M     = 1;
    PeriphClkInitStruct.PLL2.PLL2P     = 8; // 25MHz
    PeriphClkInitStruct.PLL2.PLL2Q     = 2; // 100MHz
    PeriphClkInitStruct.PLL2.PLL2R     = 1; // 200MHz
    PeriphClkInitStruct.PLL2.PLL2FRACN = 4096;


    PeriphClkInitStruct.PLL2.PLL2RGE    = RCC_PLL2VCIRANGE_2;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    // PLL 3
    PeriphClkInitStruct.PLL3.PLL3M         = 6;
    PeriphClkInitStruct.PLL3.PLL3N         = 295;
    PeriphClkInitStruct.PLL3.PLL3P         = 16; // 49.xMhz
    PeriphClkInitStruct.PLL3.PLL3Q         = 4;
    PeriphClkInitStruct.PLL3.PLL3R         = 32; // 24.xMhz
    PeriphClkInitStruct.PLL3.PLL3RGE       = RCC_PLL3VCIRANGE_1;
    PeriphClkInitStruct.PLL3.PLL3VCOSEL    = RCC_PLL3VCOWIDE;
    PeriphClkInitStruct.PLL3.PLL3FRACN     = 0;
    PeriphClkInitStruct.FmcClockSelection  = RCC_FMCCLKSOURCE_PLL2;
    PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    //PeriphClkInitStruct.SdmmcClockSelection  = RCC_SDMMCCLKSOURCE_PLL;
    PeriphClkInitStruct.SdmmcClockSelection  = RCC_SDMMCCLKSOURCE_PLL2;
    PeriphClkInitStruct.Sai1ClockSelection   = RCC_SAI1CLKSOURCE_PLL3;
    PeriphClkInitStruct.Sai23ClockSelection  = RCC_SAI23CLKSOURCE_PLL3;
    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
    PeriphClkInitStruct.Spi6ClockSelection   = RCC_SPI6CLKSOURCE_PCLK4;
    PeriphClkInitStruct.Usart234578ClockSelection
        = RCC_USART234578CLKSOURCE_D2PCLK1;
    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    PeriphClkInitStruct.I2c123ClockSelection  = RCC_I2C123CLKSOURCE_D2PCLK1;
    PeriphClkInitStruct.I2c4ClockSelection    = RCC_I2C4CLKSOURCE_PLL3;
    PeriphClkInitStruct.UsbClockSelection     = RCC_USBCLKSOURCE_HSI48;
    PeriphClkInitStruct.AdcClockSelection     = RCC_ADCCLKSOURCE_PLL3;
    if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Enable USB Voltage detector */
    HAL_PWREx_EnableUSBVoltageDetector();
}

extern "C"
{
    // Non-cacheable RAM D2 address range
    extern uint32_t _ssram1_bss, _esram1_bss;
}

// Map a region size in byte to an appropriate value
static uint8_t MPURegionSize(void* sAddr, void* eAddr)
{
    uint32_t sz = (uint8_t*)eAddr - (uint8_t*)sAddr;
    if(sz <= 1 * 1024)
        return MPU_REGION_SIZE_1KB;
    if(sz <= 2 * 1024)
        return MPU_REGION_SIZE_2KB;
    if(sz <= 4 * 1024)
        return MPU_REGION_SIZE_4KB;
    if(sz <= 8 * 1024)
        return MPU_REGION_SIZE_8KB;
    if(sz <= 16 * 1024)
        return MPU_REGION_SIZE_16KB;
    if(sz <= 32 * 1024)
        return MPU_REGION_SIZE_32KB;
    if(sz <= 64 * 1024)
        return MPU_REGION_SIZE_64KB;
    if(sz <= 128 * 1024)
        return MPU_REGION_SIZE_128KB;
    else
        return MPU_REGION_SIZE_256KB;
}

void System::ConfigureMpu()
{
    MPU_Region_InitTypeDef MPU_InitStruct;
    HAL_MPU_Disable();
    // Configure RAM D2 (SRAM1) as non cacheable
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.BaseAddress      = (uint32_t)&_ssram1_bss;
    MPU_InitStruct.Size             = MPURegionSize(&_ssram1_bss, &_esram1_bss);
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    MPU_InitStruct.IsCacheable  = MPU_ACCESS_CACHEABLE;
    MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;
    MPU_InitStruct.IsShareable  = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.Number       = MPU_REGION_NUMBER1;
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
    MPU_InitStruct.Size         = MPU_REGION_SIZE_64MB;
    MPU_InitStruct.BaseAddress  = 0xC0000000;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    // Configure the backup SRAM region as non-cacheable
    MPU_InitStruct.IsCacheable  = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsShareable  = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.Number       = MPU_REGION_NUMBER2;
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
    MPU_InitStruct.Size         = MPU_REGION_SIZE_4KB;
    MPU_InitStruct.BaseAddress  = 0x38800000;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

uint32_t System::GetSysClkFreq()
{
    return HAL_RCC_GetSysClockFreq();
}

uint32_t System::GetHClkFreq()
{
    return HAL_RCC_GetHCLKFreq();
}

uint32_t System::GetPClk1Freq()
{
    return HAL_RCC_GetPCLK1Freq();
}

uint32_t System::GetTickFreq()
{
    return HAL_RCC_GetPCLK1Freq() * 2;
}

uint32_t System::GetPClk2Freq()
{
    return HAL_RCC_GetPCLK2Freq();
}

System::MemoryRegion System::GetProgramMemoryRegion()
{
    return GetMemoryRegion(SCB->VTOR);
}

System::MemoryRegion System::GetMemoryRegion(uint32_t addr)
{
    if(addr >= D1_AXIFLASH_BASE
       && addr < D1_AXIFLASH_BASE + INTERNAL_FLASH_SIZE)
        return MemoryRegion::INTERNAL_FLASH;
    if(addr >= D1_ITCMRAM_BASE && addr < D1_ITCMRAM_BASE + ITCMRAM_SIZE)
        return MemoryRegion::ITCMRAM;
    if(addr >= D1_DTCMRAM_BASE && addr < D1_DTCMRAM_BASE + DTCMRAM_SIZE)
        return MemoryRegion::DTCMRAM;
    if(addr >= D1_AXISRAM_BASE && addr < D1_AXISRAM_BASE + SRAM_D1_SIZE)
        return MemoryRegion::SRAM_D1;
    if(addr >= D2_AXISRAM_BASE && addr < D2_AXISRAM_BASE + SRAM_D2_SIZE)
        return MemoryRegion::SRAM_D2;
    if(addr >= D3_SRAM_BASE && addr < D3_SRAM_BASE + SRAM_D3_SIZE)
        return MemoryRegion::SRAM_D3;
    if(addr >= SDRAM_BASE && addr < SDRAM_BASE + SDRAM_SIZE)
        return MemoryRegion::SDRAM;
    if(addr >= QSPI_BASE && addr < QSPI_BASE + QSPI_SIZE)
        return MemoryRegion::QSPI;

    return MemoryRegion::INVALID_ADDRESS;
}


} // namespace daisy

static void Error_Handler()
{
    // Insert code to handle errors here.
    while(1) {}
}

//////////////////////////////////////////////////
// Old C-Style API
// Delete after testing above.

//void dsy_system_init()
//{
//    //    HAL_Init();
//    //    SystemClock_Config();
//    //    MPU_Config();
//    //    dsy_dma_init();
//    //    dsy_i2c_global_init();
//    //    SCB_EnableICache();
//    //    SCB_EnableDCache();
//}
//
//void dsy_system_jumptoqspi()
//{
//    __JUMPTOQSPI();
//    while(1) {}
//}
//
//uint32_t dsy_system_getnow()
//{
//    return HAL_GetTick();
//}
//
//void dsy_system_delay(uint32_t delay_ms)
//{
//    HAL_Delay(delay_ms);
//}
//
//
//void SystemClock_Config()
//{
//    // HOLD THISSSS
//    RCC_OscInitTypeDef       RCC_OscInitStruct   = {0};
//    RCC_ClkInitTypeDef       RCC_ClkInitStruct   = {0};
//    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
//
//    /** Supply configuration update enable
//  */
//    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
//    /** Configure the main internal regulator output voltage
//  */
//    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
//    //    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
//
//    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
//    /** Macro to configure the PLL clock source
//  */
//    __HAL_RCC_PLL_PLLSOURCE_CONFIG(RCC_PLLSOURCE_HSE);
//    /** Initializes the CPU, AHB and APB busses clocks
//  */
//    RCC_OscInitStruct.OscillatorType
//        = RCC_OSCILLATORTYPE_HSI48 | RCC_OSCILLATORTYPE_HSE;
//    RCC_OscInitStruct.HSEState      = RCC_HSE_ON;
//    RCC_OscInitStruct.HSI48State    = RCC_HSI48_ON;
//    RCC_OscInitStruct.PLL.PLLState  = RCC_PLL_ON;
//    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
//    RCC_OscInitStruct.PLL.PLLM      = 4;
//    RCC_OscInitStruct.PLL.PLLN      = 200;
//    //    RCC_OscInitStruct.PLL.PLLN      = 240;
//    RCC_OscInitStruct.PLL.PLLP      = 2;
//    RCC_OscInitStruct.PLL.PLLQ      = 5; // was 4 in cube
//    RCC_OscInitStruct.PLL.PLLR      = 2;
//    RCC_OscInitStruct.PLL.PLLRGE    = RCC_PLL1VCIRANGE_2;
//    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
//    RCC_OscInitStruct.PLL.PLLFRACN  = 0;
//    if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
//    {
//        Error_Handler();
//    }
//    /** Initializes the CPU, AHB and APB busses clocks
//  */
//    RCC_ClkInitStruct.ClockType
//        = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1
//          | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
//    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
//    RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
//    RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
//    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
//    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
//    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
//    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
//
//    if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
//    {
//        Error_Handler();
//    }
//    PeriphClkInitStruct.PeriphClockSelection
//        = RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_RNG | RCC_PERIPHCLK_SPI1
//          | RCC_PERIPHCLK_SAI2 | RCC_PERIPHCLK_SAI1 | RCC_PERIPHCLK_SDMMC
//          | RCC_PERIPHCLK_I2C2 | RCC_PERIPHCLK_ADC | RCC_PERIPHCLK_I2C1
//          | RCC_PERIPHCLK_USB | RCC_PERIPHCLK_QSPI | RCC_PERIPHCLK_FMC;
//    // PLL 2
//    PeriphClkInitStruct.PLL2.PLL2M = 4;
//    //  PeriphClkInitStruct.PLL2.PLL2N = 115; // Max Freq @ 3v3
//    PeriphClkInitStruct.PLL2.PLL2N      = 84; // Max Freq @ 1V9
//    PeriphClkInitStruct.PLL2.PLL2P      = 8;  // 57.5
//    PeriphClkInitStruct.PLL2.PLL2Q      = 10; // 46
//    PeriphClkInitStruct.PLL2.PLL2R      = 2;  // 115Mhz
//    PeriphClkInitStruct.PLL2.PLL2RGE    = RCC_PLL2VCIRANGE_2;
//    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
//    PeriphClkInitStruct.PLL2.PLL2FRACN  = 0;
//    // PLL 3
//    PeriphClkInitStruct.PLL3.PLL3M        = 6;
//    PeriphClkInitStruct.PLL3.PLL3N        = 295;
//    PeriphClkInitStruct.PLL3.PLL3P        = 16; // 49.xMhz
//    PeriphClkInitStruct.PLL3.PLL3Q        = 4;
//    PeriphClkInitStruct.PLL3.PLL3R        = 32; // 24.xMhz
//    PeriphClkInitStruct.PLL3.PLL3RGE      = RCC_PLL3VCIRANGE_1;
//    PeriphClkInitStruct.PLL3.PLL3VCOSEL   = RCC_PLL3VCOWIDE;
//    PeriphClkInitStruct.PLL3.PLL3FRACN    = 0;
//    PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
//    //PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_PLL2;
//    PeriphClkInitStruct.QspiClockSelection   = RCC_QSPICLKSOURCE_D1HCLK;
//    PeriphClkInitStruct.SdmmcClockSelection  = RCC_SDMMCCLKSOURCE_PLL;
//    PeriphClkInitStruct.Sai1ClockSelection   = RCC_SAI1CLKSOURCE_PLL3;
//    PeriphClkInitStruct.Sai23ClockSelection  = RCC_SAI23CLKSOURCE_PLL3;
//    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
//    PeriphClkInitStruct.Usart234578ClockSelection
//        = RCC_USART234578CLKSOURCE_D2PCLK1;
//    //PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
//    //PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_PLL2;
//    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
//    //PeriphClkInitStruct.I2c123ClockSelection = RCC_I2C123CLKSOURCE_PLL3;
//    PeriphClkInitStruct.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
//    PeriphClkInitStruct.I2c4ClockSelection   = RCC_I2C4CLKSOURCE_PLL3;
//    //PeriphClkInitStruct.I2c4ClockSelection = RCC_I2C4CLKSOURCE_D3PCLK1;
//    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
//    //PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;
//    PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;
//    if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
//    {
//        Error_Handler();
//    }
//    /** Enable USB Voltage detector
//  */
//    HAL_PWREx_EnableUSBVoltageDetector();
//}
//
//static void MPU_Config()
//{
//    MPU_Region_InitTypeDef MPU_InitStruct;
//    HAL_MPU_Disable();
//    // Configure RAM D2 (SRAM1) as non cacheable
//    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
//    MPU_InitStruct.BaseAddress      = 0x30000000;
//    MPU_InitStruct.Size             = MPU_REGION_SIZE_32KB;
//    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
//    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
//    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
//    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
//    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
//    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
//    MPU_InitStruct.SubRegionDisable = 0x00;
//    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
//    HAL_MPU_ConfigRegion(&MPU_InitStruct);
//
//    MPU_InitStruct.IsCacheable  = MPU_ACCESS_CACHEABLE;
//    MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;
//    MPU_InitStruct.IsShareable  = MPU_ACCESS_NOT_SHAREABLE;
//    MPU_InitStruct.Number       = MPU_REGION_NUMBER1;
//    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
//    MPU_InitStruct.Size         = MPU_REGION_SIZE_64MB;
//    MPU_InitStruct.BaseAddress  = 0xC0000000;
//    HAL_MPU_ConfigRegion(&MPU_InitStruct);
//
//    //    uint32_t enable_bit = (1 << 0);
//    //    uint32_t privdefena = (1 << 2);
//    //    uint32_t rasr_enable = (1 << 0);
//
//
//    //    MPU->CTRL = enable_bit | privdefena;
//    //    MPU->RNR  = 0x00;
//    //    MPU->RBAR = 0x30000000;
//    //    MPU->RASR = rasr_enable;
//
//
//    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
//}
#else // ifndef UNIT_TEST

#include "system.h"
// this is part of the dummy version used in unit tests
TestIsolator<daisy::System::SystemState> daisy::System::testIsolator_;

#endif
