#pragma once
#include "../core/types.h"

namespace oceanblast {

// S3C2410 Memory Map
constexpr u32 ADDR_STEPPINGSTONE_BASE = 0x00000000;
constexpr u32 ADDR_STEPPINGSTONE_SIZE = 4096; // 4 KB internal boot SRAM

constexpr u32 ADDR_SDRAM_BASE         = 0x30000000;
constexpr u32 ADDR_SDRAM_SIZE         = 32 * 1024 * 1024; // 32 MB SDRAM

// S3C2410 MMIO Peripheral Bases
constexpr u32 ADDR_MEMCON_BASE        = 0x48000000;
constexpr u32 ADDR_USBHOST_BASE       = 0x49000000;
constexpr u32 ADDR_INTCON_BASE        = 0x4A000000;
constexpr u32 ADDR_DMA_BASE           = 0x4B000000;
constexpr u32 ADDR_CLKCON_BASE        = 0x4C000000;
constexpr u32 ADDR_LCDCON_BASE        = 0x4D000000;
constexpr u32 ADDR_NANDCON_BASE       = 0x4E000000;
constexpr u32 ADDR_UART_BASE          = 0x50000000;
constexpr u32 ADDR_TIMER_BASE         = 0x51000000;
constexpr u32 ADDR_USBD_BASE          = 0x52000000;
constexpr u32 ADDR_WDT_BASE           = 0x53000000;
constexpr u32 ADDR_IIC_BASE           = 0x54000000;
constexpr u32 ADDR_IIS_BASE           = 0x55000000;
constexpr u32 ADDR_GPIO_BASE          = 0x56000000;
constexpr u32 ADDR_RTC_BASE           = 0x57000000;
constexpr u32 ADDR_ADC_BASE           = 0x58000000;
constexpr u32 ADDR_SPI_BASE           = 0x59000000;
constexpr u32 ADDR_SDI_BASE           = 0x5A000000;

} // namespace oceanblast
