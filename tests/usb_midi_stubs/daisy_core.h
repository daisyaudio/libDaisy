#pragma once

/* Host tests use ordinary memory in place of the DMA linker section. */
#define DMA_BUFFER_MEM_SECTION
#define UNUSED(value) (void)(value)
