/**    
 * Copyright (C) Johannes Elliesen, 2021
 */

#pragma once

#include <string_view>
#include <algorithm>

namespace daisy
{
template <class CharType = char>
class FixedCapStrBase
{
  public:
    constexpr FixedCapStrBase(CharType* buffer, size_t capacity)
    : capacity_(capacity), buffer_(buffer)
    {
    }

    constexpr FixedCapStrBase(const FixedCapStrBase& other) = delete;

    constexpr FixedCapStrBase& operator=(const FixedCapStrBase& str)
    {
        size_ = std::min(str.Size(), capacity_);
        Copy_(str.Data(), str.Data() + size_, buffer_);
        buffer_[size_] = 0;
        return *this;
    }

    constexpr FixedCapStrBase& operator=(const CharType* str)
    {
        size_ = std::min(strlen(str), capacity_);
        Copy_(str, str + size_, buffer_);
        buffer_[size_] = 0;
        return *this;
    }

    constexpr operator const CharType*() const noexcept { return buffer_; }
    constexpr const CharType* Cstr() const noexcept { return buffer_; }
    constexpr const CharType* Data() const noexcept { return buffer_; }
    constexpr CharType*       Data() noexcept { return buffer_; }

    constexpr auto Size() const noexcept { return size_; }
    constexpr auto UpdateSize() noexcept
    {
        size_          = std::min(strlen(buffer_), capacity_);
        buffer_[size_] = 0;
    }

    constexpr auto Capacity() const noexcept { return capacity_; }

    constexpr auto Empty() const noexcept { return size_ == 0; }

    constexpr void Clear() noexcept
    {
        size_      = 0;
        buffer_[0] = '\0';
    }

    constexpr void Reset(const CharType* str)
    {
        size_ = std::min(strlen(str), capacity_);
        Reset_(str, size_);
    }

    constexpr void Reset(const CharType* str, std::size_t length)
    {
        size_ = std::min(length, capacity_);
        Reset_(str, size_);
    }

    constexpr void ResetAt(const CharType* str, std::size_t writePosition)
    {
        if(writePosition > size_)
            return;
        const auto strLen = strlen(str);
        const auto newSize
            = std::max(std::min(strLen + writePosition, capacity_), size_);
        const auto numCharsToWrite = std::min(newSize - writePosition, strLen);
        size_                      = newSize;
        ResetAt_(str, numCharsToWrite, writePosition);
    }

    constexpr void Append(const CharType singleChar)
    {
        if(size_ < capacity_)
        {
            buffer_[size_]     = singleChar;
            buffer_[size_ + 1] = '\0';
            size_++;
        }
    }

    constexpr void Append(const CharType* str)
    {
        auto to_copy = std::min(strlen(str), (capacity_ - size_));
        Append_(str, to_copy);
    }

    constexpr void Append(const CharType* str, std::size_t length)
    {
        auto to_copy = std::min(length, (capacity_ - size_));
        Append_(str, to_copy);
    }

    template <typename IntType>
    constexpr void AppendInt(IntType value, bool alwaysIncludeSign = false)
    {
        if(value == 0)
        {
            if(alwaysIncludeSign)
                Append('+');
            Append("0");
            return;
        }

        if(value < 0)
        {
            value = -value;
            Append('-');
        }
        else if(alwaysIncludeSign)
            Append('+');

        const auto firstDigit = Size();
        while(value != 0)
        {
            const auto rem = value % 10;
            value          = value / 10;
            Append(rem + '0');
        }
        const auto lastDigit = Size() - 1;
        ReverseSection(firstDigit, lastDigit);
    }

    constexpr void AppendFloat(float value,
                               int   maxNumDigits      = 2,
                               bool  omitTrailingZeros = false,
                               bool  alwaysIncludeSign = false)
    {
        if(value == 0.0f)
        {
            if(alwaysIncludeSign)
                Append('+');
            Append("0");
            if((!omitTrailingZeros) && (maxNumDigits > 0))
            {
                Append(".");
                for(int i = 0; i < maxNumDigits; i++)
                    Append("0");
            }
            return;
        }

        if(value < 0)
        {
            value = -value;
            Append('-');
        }
        else if(alwaysIncludeSign)
            Append('+');

        float         factor    = 1;
        constexpr int tableSize = 10;
        // clang-format off
        constexpr float powTable[tableSize] = {
            1.0f,
            10.0f,
            100.0f,
            1000.0f,
            10000.0f,
            100000.0f,
            1000000.0f,
            10000000.0f,
            100000000.0f,
            1000000000.0f
        };
        constexpr float roundOffsTable[tableSize] = {
            0.5f,
            0.05f,
            0.005f,
            0.0005f,
            0.00005f,
            0.000005f,
            0.0000005f,
            0.00000005f,
            0.000000005f,
        };
        // clang-format on
        if(maxNumDigits <= tableSize)
        {
            factor = powTable[maxNumDigits];
            value += roundOffsTable[maxNumDigits];
        }
        else
        {
            float roundOffs = 0.5f;
            for(int i = 0; i < maxNumDigits; i++)
            {
                factor *= 10.0f;
                roundOffs /= 10.0f;
            }
            value += roundOffs;
        }

        int beforeDecPt = int(value);
        int afterDecPt  = int(value * factor);

        const auto firstDigit = Size();
        // print digits after the decimal point
        for(int i = 0; i < maxNumDigits; i++)
        {
            const auto rem = afterDecPt % 10;
            afterDecPt     = afterDecPt / 10;
            if((rem == 0) && (omitTrailingZeros))
                continue;
            Append(rem + '0');
        }
        // print decimal point
        if(Size() != firstDigit)
            Append('.');
        // print digits before the decimal point
        if(beforeDecPt == 0)
            Append('0');
        else
        {
            while(beforeDecPt != 0)
            {
                const auto rem = beforeDecPt % 10;
                beforeDecPt    = beforeDecPt / 10;
                Append(rem + '0');
            }
        }
        const auto lastDigit = Size() - 1;
        ReverseSection(firstDigit, lastDigit);
    }

    constexpr bool StartsWith(const CharType* pattern) const noexcept
    {
        const CharType* ptr = buffer_;
        while(*pattern)
        {
            if(*ptr != *pattern)
                return false;
            pattern++;
            ptr++;
        }
        return true;
    }

    constexpr bool
    StartsWithIgnoringCase(const CharType* pattern) const noexcept
    {
        const CharType* ptr = buffer_;
        while(*pattern)
        {
            if(ToUpper_(*ptr) != ToUpper_(*pattern))
                return false;
            pattern++;
            ptr++;
        }
        return true;
    }

    constexpr bool EndsWith(const CharType* pattern) const noexcept
    {
        const CharType* ptr           = &buffer_[size_ - 1];
        const auto      patternLength = strlen(pattern);
        const CharType* patternPtr    = pattern + patternLength - 1;
        while(patternPtr > pattern)
        {
            if(*ptr != *patternPtr)
                return false;
            patternPtr--;
            ptr--;
        }
        return *ptr == *patternPtr;
    }

    constexpr bool EndsWithIgnoringCase(const CharType* pattern) const noexcept
    {
        const CharType* ptr           = &buffer_[size_ - 1];
        const auto      patternLength = strlen(pattern);
        const CharType* patternPtr    = pattern + patternLength - 1;
        while(patternPtr > pattern)
        {
            if(ToUpper_(*ptr) != ToUpper_(*patternPtr))
                return false;
            patternPtr--;
            ptr--;
        }
        return ToUpper_(*ptr) == ToUpper_(*patternPtr);
    }

    constexpr void RemovePrefix(std::size_t length)
    {
        Copy_(buffer_ + length, buffer_ + size_, buffer_);
        size_ -= length;
        buffer_[size_] = '\0';
    }

    constexpr void RemoveSuffix(std::size_t length) noexcept
    {
        size_          = size_ - length;
        buffer_[size_] = '\0';
    }

    constexpr void ReverseSection(std::size_t firstIdx, std::size_t lastIdx)
    {
        firstIdx = std::clamp<std::size_t>(firstIdx, 0, size_ - 1);
        lastIdx  = std::clamp<std::size_t>(lastIdx, 0, size_ - 1);
        while(firstIdx < lastIdx)
        {
            CharType tmp      = buffer_[lastIdx];
            buffer_[lastIdx]  = buffer_[firstIdx];
            buffer_[firstIdx] = tmp;
            firstIdx++;
            lastIdx--;
        }
    }

    constexpr bool operator==(const CharType* rhs) const
    {
        const CharType* ptr = buffer_;
        while(*rhs && *ptr) // abort on first string end
        {
            if(*ptr != *rhs)
                return false;
            rhs++;
            ptr++;
        }

        return *rhs == *ptr; // both strings ended at the same '0'?
    }

    constexpr bool operator!=(const CharType* rhs) const
    {
        return !(*this == rhs);
    }

    constexpr bool operator<(const CharType* other) const
    {
        auto ptr = buffer_;
        while(*ptr && *other && (*ptr == *other))
        {
            ptr++;
            other++;
        }
        return *ptr < *other;
    }

    constexpr bool operator<=(const CharType* other) const
    {
        return (*this == other) || (*this < other);
    }

    constexpr bool operator>(const CharType* other) const
    {
        auto ptr = buffer_;
        while(*ptr && *other && (*ptr == *other))
        {
            ptr++;
            other++;
        }
        return *ptr > *other;
    }

    constexpr bool operator>=(const CharType* other) const
    {
        return (*this == other) || (*this > other);
    }

    constexpr void Swap(FixedCapStrBase& rhs) noexcept
    {
        auto tmp  = size_;
        size_     = rhs.size_;
        rhs.size_ = tmp;

        Swap_(buffer_, rhs.buffer_, std::max(size_, rhs.size_));
    }

  protected:
    static constexpr std::size_t strlen(const CharType* string)
    {
        std::size_t result = 0;
        while(*string++ != '\0')
            result++;
        return result;
    }

    constexpr void Reset_(const CharType* str, std::size_t length)
    {
        Copy_(str, str + length, buffer_);
        buffer_[length] = '\0';
    }

    constexpr void
    ResetAt_(const CharType* str, std::size_t strLen, std::size_t writePosition)
    {
        Copy_(str, str + strLen, buffer_ + writePosition);
        if(writePosition + strLen > size_)
            buffer_[writePosition + strLen] = '\0';
    }

    constexpr void Append_(const CharType* str, std::size_t to_copy)
    {
        Copy_(str, str + to_copy, buffer_ + size_);
        size_ += to_copy;
        buffer_[size_] = '\0';
    }

    static constexpr void
    Copy_(const CharType* src, const CharType* srcEnd, CharType* dest)
    {
        while(src != srcEnd)
        {
            *dest = *src;
            src++;
            dest++;
        }
    }

    static constexpr void Swap_(CharType* a, CharType* b, size_t length)
    {
        for(size_t i = 0; i < length; i++)
        {
            CharType tmp = *a;
            *a           = *b;
            *b           = tmp;
            a++;
            b++;
        }
    }

    // TODO: add wstring version
    static constexpr char ToUpper_(char c) noexcept
    {
        switch(c)
        {
            default: return c;
            case 'a': return 'A';
            case 'b': return 'B';
            case 'c': return 'C';
            case 'd': return 'D';
            case 'e': return 'E';
            case 'f': return 'F';
            case 'g': return 'G';
            case 'h': return 'H';
            case 'i': return 'I';
            case 'j': return 'J';
            case 'k': return 'K';
            case 'l': return 'L';
            case 'm': return 'M';
            case 'n': return 'N';
            case 'o': return 'O';
            case 'p': return 'P';
            case 'q': return 'Q';
            case 'r': return 'R';
            case 's': return 'S';
            case 't': return 'T';
            case 'u': return 'U';
            case 'v': return 'V';
            case 'w': return 'W';
            case 'x': return 'X';
            case 'y': return 'Y';
            case 'z': return 'Z';
        }
    }

    std::size_t  size_{0};
    const size_t capacity_;
    CharType*    buffer_;
};

/** @brief A safe and convenient statically allocated string with constexpr powers
 *  @author jelliesen
 *  @addtogroup utility
 * 
 * This string class is statically allocated. All of its functions can be evaluated at compile time
 * through the power of constexpr.
 */
template <std::size_t capacity, class CharType = char>
class FixedCapStr : public FixedCapStrBase<CharType>
{
  public:
    constexpr FixedCapStr() noexcept
    : FixedCapStrBase<CharType>(buffer_, capacity)
    {
    }

    constexpr FixedCapStr(const FixedCapStr& str) noexcept
    : FixedCapStrBase<CharType>(buffer_, capacity)
    {
        this->size_ = std::min(str.Size(), capacity);
        this->Copy_(str.Data(), str.Data() + this->size_, buffer_);
    }

    template <size_t otherSize>
    constexpr FixedCapStr(const FixedCapStr<otherSize>& str) noexcept
    : FixedCapStrBase<CharType>(buffer_, capacity)
    {
        this->size_ = std::min(str.Size(), capacity);
        this->Copy_(str.Data(), str.Data() + this->size_, buffer_);
    }

    constexpr FixedCapStr(const CharType* str) noexcept
    : FixedCapStrBase<CharType>(buffer_, capacity)
    {
        this->size_ = std::min(this->strlen(str), capacity);
        this->Copy_(str, str + this->size_, buffer_);
    }

    constexpr FixedCapStr(const CharType* str, std::size_t length) noexcept
    : FixedCapStrBase<CharType>(buffer_, capacity)
    {
        this->size_ = std::min(length, capacity);
        this->Copy_(str, str + this->size_, buffer_);
    }

    constexpr FixedCapStr& operator=(const FixedCapStr& str) noexcept
    {
        *static_cast<FixedCapStrBase<CharType>*>(this) = str;
        return *this;
    }

  private:
    CharType buffer_[capacity + 1]{};
};

template <class CharType, std::size_t capacity>
inline constexpr void Swap(const FixedCapStr<capacity, CharType>& lhs,
                           const FixedCapStr<capacity, CharType>& rhs) noexcept
{
    rhs.Swap(lhs);
}

} // namespace daisy
