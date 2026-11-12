#pragma once
#ifndef __DSY_LOGGER_H__
#define __DSY_LOGGER_H__

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <type_traits>
#include "logger_impl.h"

namespace daisy
{
/**
 *  @addtogroup hid_logging LOGGING
 *  @brief Intefaces for Logging over USB, etc.
 *  @ingroup human_interface
 *  @ingroup libdaisy
 *
 *  The following is a short example of using the DaisySeed::Logger
 *  to print to a serial port.
 *  @include SerialPrint.cpp
 *  @{
 */

/** @defgroup logging_macros LoggerHelperMacros
 *  @{ */
/** Logger configuration
 */
#define LOGGER_NEWLINE "\r\n" /**< custom newline character sequence */
#define LOGGER_BUFFER 128     /**< size in bytes */

/**
 * Helper macros for string concatenation and macro expansion
 */
#define PPCAT_NX(A, B) A##B          /**< non-expanding concatenation */
#define PPCAT(A, B) PPCAT_NX(A, B)   /**< concatenate tokens */
#define STRINGIZE_NX(A) #A           /**< non-expanding stringize  */
#define STRINGIZE(A) STRINGIZE_NX(A) /**< make a string */

/** Floating point output formatting string. Include in your printf-style format string
 *  example: printf("float value = " FLT_FMT(3) " continue like that", FLT_VAR(3, x));
 */
// clang-format off
#define FLT_FMT(_n) STRINGIZE(PPCAT(PPCAT(%c%d.%0, _n), d))
// clang-format on

/** Floating point output variable preprocessing
 * Note: uses truncation instead of rounding -> the last digit may be off
 */
#define FLT_VAR(_n, _x)                   \
    (_x < 0 ? '-' : ' '), (int)(abs(_x)), \
        (int)(((abs(_x)) - (int)(abs(_x))) * pow(10, (_n)))

/** Shorthand for 10^-3 fraction, output equivalent to %.3f
 */
#define FLT_FMT3 FLT_FMT(3)
/** Shorthand for 10^-3 fraction
 */
#define FLT_VAR3(_x) FLT_VAR(3, _x)

/** @} */ // End logging_macros

/**   @brief Interface for simple USB logging
 *    @author Alexander Petrov-Savchenko (axp@soft-amp.com)
 *    @date November 2020
 *
 *    Simple Example:
 *    @include SerialPrint.cpp
 *
 */
template <LoggerDestination dest = LOGGER_INTERNAL>
class Logger
{
  public:
    /** Object constructor
     */
    Logger() {}

    /** Print formatted string
     */
    static void Print(const char* format, ...);

    /** Print formatted string appending line termination sequence
     */
    static void PrintLine(const char* format, ...);

    /**  Start the logging session.
     * \param wait_for_pc block until remote terminal is ready
     * (this param has no effect when using the UART destination)
     */
    static void StartLog(bool wait_for_pc = false);

    /** Variadic argument variant of Print()
     */
    static void PrintV(const char* format, va_list va);

    /** Variadic argument variant of PrintLine()
     */
    static void PrintLineV(const char* format, va_list va);

    /** Configuration specialization for UART Logging
     *  Only available for UART logger.
     */
    template <LoggerDestination D = dest>
    static std::enable_if_t<D == LOGGER_UART>
    Configure(const LoggerImpl<LOGGER_UART>::Config cfg)
    {
        impl_.Configure(cfg);
    }


  protected:
    /** Internal constants
     */
    enum LoggerConsts
    {
        LOGGER_SYNC_OUT = 0,
        LOGGER_SYNC_IN  = 2 /**< successfully transmit this many packets
                             * before switching to blocking transfers */
    };

    /** Blocking wrapper for Transmit()
     */
    static void TransmitSync(const void* buffer, size_t bytes)
    {
        while(false == impl_.Transmit(buffer, bytes)) {}
    }

    /** Transfer accumulated data
     */
    static void TransmitBuf();

    /** Trim control characters and append clean newline sequence, if there's room in the buffer
     */
    static void AppendNewLine();

    /** Constexpr function equivalent of strlen(LOGGER_NEWLINE)
     */
    static constexpr size_t NewLineSeqLength()
    {
        size_t                len = 0;
        constexpr const char* nl  = LOGGER_NEWLINE;
        while(nl[len] != '\0')
        {
            len++;
        }
        return len;
    }

    /** member variables
     */

    /** this needs to remain in SRAM to support startup-time printouts
     */
    inline static char   tx_buff_[LOGGER_BUFFER]; /**< buffer for log data */
    inline static size_t tx_ptr_ = 0; /**< current position in the buffer */
    /** start with non-blocking transfers to support startup-time printouts
     */
    inline static size_t pc_sync_
        = LOGGER_SYNC_OUT; /**< terminal synchronization state */
    inline static LoggerImpl<dest>
        impl_; /**< underlying trasnfer implementation */
};

/** Specialization for a muted log
 */
template <>
class Logger<LOGGER_NONE>
{
  public:
    Logger() {}                                               /**<  */
    static void Print(const char* format, ...) {}             /**<  */
    static void PrintLine(const char* format, ...) {}         /**<  */
    static void StartLog(bool wait_for_pc = false) {}         /**<  */
    static void PrintV(const char* format, va_list va) {}     /**<  */
    static void PrintLineV(const char* format, va_list va) {} /**<  */
};

/** @} */
} // namespace daisy

#endif // __DSY_LOGGER_H__
