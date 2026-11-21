#include "FatFsMock.h"

#include <cstring>

namespace
{
test::FatFsMockState g_state;
}

namespace test
{
FatFsMockState& GetFatFsMockState()
{
    return g_state;
}

void ResetFatFsMockState()
{
    g_state.file_data.clear();
    g_state.cursor      = 0;
    g_state.open_count  = 0;
    g_state.close_count = 0;
}
} // namespace test

extern "C"
{
FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode)
{
    (void)fp;
    (void)path;
    (void)mode;
    test::ResetFatFsMockState();
    test::GetFatFsMockState().open_count++;
    return FR_OK;
}

FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw)
{
    (void)fp;
    auto& state            = test::GetFatFsMockState();
    const size_t start_idx = static_cast<size_t>(state.cursor);
    const size_t end_idx   = start_idx + static_cast<size_t>(btw);
    if(state.file_data.size() < end_idx)
        state.file_data.resize(end_idx, 0);

    if(btw > 0)
        std::memcpy(&state.file_data[start_idx], buff, static_cast<size_t>(btw));
    state.cursor += btw;
    if(bw)
        *bw = btw;
    return FR_OK;
}

FRESULT f_lseek(FIL* fp, FSIZE_t ofs)
{
    (void)fp;
    test::GetFatFsMockState().cursor = ofs;
    return FR_OK;
}

FRESULT f_close(FIL* fp)
{
    (void)fp;
    test::GetFatFsMockState().close_count++;
    return FR_OK;
}
}

