#include "gpio.h"
#include "stm32h7xx_hal.h"

using namespace daisy;

void GPIO::Init(const Config &cfg)
{
    /** Copy Config */
    cfg_ = cfg;

    Init();
}

void GPIO::Init()
{
    if(!cfg_.pin.IsValid())
        return;

    GPIO_InitTypeDef ginit;
    switch(cfg_.mode)
    {
        case Mode::OUTPUT: ginit.Mode = GPIO_MODE_OUTPUT_PP; break;
        case Mode::OPEN_DRAIN: ginit.Mode = GPIO_MODE_OUTPUT_OD; break;
        case Mode::ANALOG: ginit.Mode = GPIO_MODE_ANALOG; break;
        case Mode::INPUT:
        default: ginit.Mode = GPIO_MODE_INPUT; break;
    }
    switch(cfg_.pull)
    {
        case Pull::PULLUP: ginit.Pull = GPIO_PULLUP; break;
        case Pull::PULLDOWN: ginit.Pull = GPIO_PULLDOWN; break;
        case Pull::NOPULL:
        default: ginit.Pull = GPIO_NOPULL;
    }
    switch(cfg_.speed)
    {
        case Speed::VERY_HIGH: ginit.Speed = GPIO_SPEED_FREQ_VERY_HIGH; break;
        case Speed::HIGH: ginit.Speed = GPIO_SPEED_FREQ_HIGH; break;
        case Speed::MEDIUM: ginit.Speed = GPIO_SPEED_FREQ_MEDIUM; break;
        case Speed::LOW:
        default: ginit.Speed = GPIO_SPEED_FREQ_LOW;
    }

    port_base_addr_ = GetGPIOBaseRegister();
    /** Start Relevant GPIO clock */
    switch(cfg_.pin.port)
    {
        case PORTA: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
        case PORTB: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
        case PORTC: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
        case PORTD: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
        case PORTE: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
        case PORTF: __HAL_RCC_GPIOF_CLK_ENABLE(); break;
        case PORTG: __HAL_RCC_GPIOG_CLK_ENABLE(); break;
        case PORTH: __HAL_RCC_GPIOH_CLK_ENABLE(); break;
        case PORTI: __HAL_RCC_GPIOI_CLK_ENABLE(); break;
        case PORTJ: __HAL_RCC_GPIOJ_CLK_ENABLE(); break;
        case PORTK: __HAL_RCC_GPIOK_CLK_ENABLE(); break;
        default: break;
    }
    /** Set pin based on stm32 schema */
    ginit.Pin = (1 << cfg_.pin.pin);
    HAL_GPIO_Init((GPIO_TypeDef *)port_base_addr_, &ginit);
}
void GPIO::Init(Pin p, const Config &cfg)
{
    /** Copy config */
    cfg_ = cfg;
    /** Overwrite with explicit pin */
    cfg_.pin = p;
    Init(cfg_);
}
void GPIO::Init(Pin p, Mode m, Pull pu, Speed sp)
{
    // Populate Config struct, and init with overload
    cfg_.pin   = p;
    cfg_.mode  = m;
    cfg_.pull  = pu;
    cfg_.speed = sp;
    Init(cfg_);
}

void GPIO::DeInit()
{
    if(cfg_.pin.IsValid())
        HAL_GPIO_DeInit((GPIO_TypeDef *)port_base_addr_, (1 << cfg_.pin.pin));
}
bool GPIO::Read()
{
    if(!cfg_.pin.IsValid())
        return false;
    return HAL_GPIO_ReadPin((GPIO_TypeDef *)port_base_addr_,
                            (1 << cfg_.pin.pin));
}
void GPIO::Write(bool state)
{
    if(!cfg_.pin.IsValid())
        return;
    HAL_GPIO_WritePin((GPIO_TypeDef *)port_base_addr_,
                      (1 << cfg_.pin.pin),
                      (GPIO_PinState)state);
}
void GPIO::Toggle()
{
    if(!cfg_.pin.IsValid())
        return;
    HAL_GPIO_TogglePin((GPIO_TypeDef *)port_base_addr_, (1 << cfg_.pin.pin));
}

uint32_t *GPIO::GetGPIOBaseRegister()
{
    switch(cfg_.pin.port)
    {
        case PORTA: return (uint32_t *)GPIOA;
        case PORTB: return (uint32_t *)GPIOB;
        case PORTC: return (uint32_t *)GPIOC;
        case PORTD: return (uint32_t *)GPIOD;
        case PORTE: return (uint32_t *)GPIOE;
        case PORTF: return (uint32_t *)GPIOF;
        case PORTG: return (uint32_t *)GPIOG;
        case PORTH: return (uint32_t *)GPIOH;
        case PORTI: return (uint32_t *)GPIOI;
        case PORTJ: return (uint32_t *)GPIOJ;
        case PORTK: return (uint32_t *)GPIOK;
        default: return NULL;
    }
}
