/**
 * @file hal.h
 * @author Mitch Weisbrod
 * @brief This is the main Hardware Abstraction Layer (HAL) file for the
 * library. It contains functions which are considered part of the main API but
 * are hardware dependent. This file is included by esp_dmx.h but its source
 * file differs depending on the microcontroller on which it is implemented.
 */
#pragma once

#include "dmx_types.h"
#include "freertos/FreeRTOS.h"
#include "rdm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The major version number of this library. (X.x.x)*/
#define ESP_DMX_VERSION_MAJOR 3

/** @brief The minor version number of this library. (x.X.x)*/
#define ESP_DMX_VERSION_MINOR 1

/** @brief The patch version number of this library. (x.x.X)*/
#define ESP_DMX_VERSION_PATCH 0

/** @brief The version of this library expressed as a 32-bit integer value.*/
#define ESP_DMX_VERSION_ID                                        \
  ((ESP_DMX_VERSION_MAJOR << 16) | (ESP_DMX_VERSION_MINOR << 8) | \
   ESP_DMX_VERSION_PATCH)

/** @brief The version of this library expressed as a string value.*/
#define ESP_DMX_VERSION_LABEL                                 \
  "esp_dmx v" __XSTRING(ESP_DMX_VERSION_MAJOR) "." __XSTRING( \
      ESP_DMX_VERSION_MINOR) "." __XSTRING(ESP_DMX_VERSION_PATCH)

/** @brief The default configuration for the DMX driver. Passing this
 * configuration to dmx_driver_install() installs the driver with one DMX
 * personality which has a footprint of one DMX address. The DMX address will
 * automatically be searched for in NVS and set to 1 if not found or if NVS is
 * disabled. */
#define DMX_CONFIG_DEFAULT                                     \
  ((dmx_config_t){                                             \
      255,                          /*alloc_size*/             \
      0,                            /*model_id*/               \
      RDM_PRODUCT_CATEGORY_FIXTURE, /*product_category*/       \
      ESP_DMX_VERSION_ID,           /*software_version_id*/    \
      ESP_DMX_VERSION_LABEL,        /*software_version_label*/ \
      1,                            /*current_personality*/    \
      {{1, "Default Personality"}}, /*personalities*/          \
      1,                            /*personality_count*/      \
      0,                            /*dmx_start_address*/      \
  })

#if defined(CONFIG_DMX_ISR_IN_IRAM) || ESP_IDF_VERSION_MAJOR < 5
/** @brief This macro sets certain functions used within DMX interrupt handlers
 * to be placed within IRAM. The current hardware configuration of this device
 * places the DMX driver functions within IRAM. */
#define DMX_ISR_ATTR IRAM_ATTR
/** @brief This macro is used to conditionally compile certain parts of code
 * depending on whether or not the DMX driver is within IRAM.*/
#define DMX_ISR_IN_IRAM
#else
/** @brief This macro sets certain functions used within DMX interrupt handlers
 * to be placed within IRAM. Due to the current hardware configuration of this
 * device, the DMX driver is not currently placed within IRAM. */
#define DMX_ISR_ATTR
#endif

#ifdef DMX_ISR_IN_IRAM
/** @brief The default interrupt flags for the DMX sniffer. Places the
 * interrupts in IRAM.*/
#define DMX_SNIFFER_INTR_FLAGS_DEFAULT (ESP_INTR_FLAG_EDGE | ESP_INTR_FLAG_IRAM)
/** @brief The default interrupt flags for the DMX driver. Places the
 * interrupts in IRAM.*/
#define DMX_INTR_FLAGS_DEFAULT (ESP_INTR_FLAG_IRAM)
#else
/** @brief The default interrupt flags for the DMX sniffer.*/
#define DMX_SNIFFER_INTR_FLAGS_DEFAULT (ESP_INTR_FLAG_EDGE)
/** @brief The default interrupt flags for the DMX driver.*/
#define DMX_INTR_FLAGS_DEFAULT (0)
#endif

/**
 * @brief Installs the DMX driver and sets the default configuration. To
 * generate the DMX reset sequence, users may choose to use either the hardware
 * timers or busy-waiting. The default configuration sets the DMX break to 176
 * microseconds and the DMX mark-after-break to 12 microseconds.
 *
 * @note By default, the DMX driver will allocate a hardware timer for the DMX
 * driver to use. When using ESP-IDF v4.4 the DMX driver will allocate a
 * hardware timer group and timer relative to the DMX port number. The function
 * to determine which timer group and number to use is
 * timer_group == (dmx_num / 2) and timer_num == (dmx_num % 2). It is not
 * recommended to use the hardware timer that the DMX driver is using while the
 * DMX driver is installed. On the ESP32-C3, hardware timer number 0 will always
 * be used to avoid clobbering the watchdog timer.
 *
 * @note The DMX interrupt service routine is installed on the same CPU core
 * that this function is running on.
 *
 * @param dmx_num The DMX port number.
 * @param[in] config A pointer to a DMX configuration which will be used to
 * setup the DMX driver.
 * @param intr_flags The interrupt allocation flags to use.
 * @return true on success.
 * @return false on failure.
 */
bool dmx_driver_install(dmx_port_t dmx_num, const dmx_config_t *config,
                        int intr_flags);

/**
 * @brief Uninstalls the DMX driver.
 *
 * @param dmx_num The DMX port number
 * @return true on success.
 * @return false on failure.
 */
bool dmx_driver_delete(dmx_port_t dmx_num);

/**
 * @brief Disables the DMX driver. When the DMX driver is not placed in IRAM,
 * functions which disable the cache, such as functions which read or write to
 * flash, will also stop DMX interrupts from firing. This can cause incoming DMX
 * data to become corrupted. To avoid this issue, the DMX driver should be
 * disabled before disabling the cache. When cache is reenabled, the DMX driver
 * can be reenabled as well. When the DMX driver is placed in IRAM, disabling
 * and reenabling the DMX driver is not needed.
 *
 * @param dmx_num The DMX port number.
 * @return true on success.
 * @return false on failure.
 */
bool dmx_driver_disable(dmx_port_t dmx_num);

/**
 * @brief Enables the DMX driver. When the DMX driver is not placed in IRAM,
 * functions which disable the cache, such as functions which read or write to
 * flash, will also stop DMX interrupts from firing. This can cause incoming DMX
 * data to become corrupted. To avoid this issue, the DMX driver should be
 * disabled before disabling the cache. When cache is reenabled, the DMX driver
 * can be reenabled as well. When the DMX driver is placed in IRAM, disabling
 * and reenabling the DMX driver is not needed.
 *
 * @param dmx_num The DMX port number.
 * @return true on success.
 * @return false on failure.
 */
bool dmx_driver_enable(dmx_port_t dmx_num);

/**
 * @brief Sets DMX pin number.
 *
 * @param dmx_num The DMX port number.
 * @param tx_pin The pin to which the TX signal will be assigned.
 * @param rx_pin The pin to which the RX signal will be assigned.
 * @param rts_pin The pin to which the RTS signal will be assigned.
 * @return true on success.
 * @return false on failure.
 */
bool dmx_set_pin(dmx_port_t dmx_num, int tx_pin, int rx_pin, int rts_pin);

/**
 * @brief Sets the DMX baud rate. The baud rate will be clamped to DMX
 * specification. If the input baud rate is lower than DMX_MIN_BAUD_RATE it will
 * be set to DMX_MIN_BAUD_RATE. If the input baud rate is higher than
 * DMX_MAX_BAUD_RATE it will be set to DMX_MAX_BAUD_RATE.
 *
 * @param dmx_num The DMX port number.
 * @param baud_rate The baud rate to which to set the DMX driver.
 * @return the value that the baud rate was set to or 0 on error.
 */
uint32_t dmx_set_baud_rate(dmx_port_t dmx_num, uint32_t baud_rate);

/**
 * @brief Gets the DMX baud rate.
 *
 * @param dmx_num The DMX port number.
 * @return the current baud rate or 0 on error.
 */
uint32_t dmx_get_baud_rate(dmx_port_t dmx_num);

/**
 * @brief Sets the DMX start address of the DMX driver. The DMX start address
 * cannot be set if it is set to DMX_START_ADDRESS_NONE.
 *
 * @param dmx_num The DMX port number.
 * @param dmx_start_address The start address at which to set the DMX driver.
 * @return true on success.
 * @return false on failure.
 */
bool dmx_set_start_address(dmx_port_t dmx_num, uint16_t dmx_start_address);

/**
 * @brief Writes DMX data from a source buffer into the DMX driver buffer with
 * an offset. Allows a source buffer to be written to a specific slot number in
 * the DMX driver buffer.
 *
 * @param dmx_num The DMX port number.
 * @param offset The number of slots with which to offset the write. If set to 0
 * this function is equivalent to dmx_write().
 * @param[in] source The source buffer which is copied to the DMX driver.
 * @param size The size of the source buffer.
 * @return The number of bytes written into the DMX driver.
 */
size_t dmx_write_offset(dmx_port_t dmx_num, size_t offset, const void *source,
                        size_t size);

/**
 * @brief Receives a DMX packet from the DMX bus. This is a blocking function.
 * This function first blocks until the DMX driver is idle and then it blocks
 * using a timeout until a new packet is received. This function will timeout
 * early according to RDM specification if an RDM packet is expected.
 *
 * @note This function uses FreeRTOS direct-to-task notifications to block and
 * unblock. Using task notifications on the same task that calls this function
 * can lead to undesired behavior and program instability.
 *
 * @param dmx_num The DMX port number.
 * @param[out] packet An optional pointer to a dmx_packet_t which contains
 * information about the received DMX packet.
 * @param wait_ticks The number of ticks to wait before this function times out.
 * @return The size of the received DMX packet or 0 if no packet was received.
 */
size_t dmx_receive(dmx_port_t dmx_num, dmx_packet_t *packet,
                   TickType_t wait_ticks);

/**
 * @brief Sends a DMX packet on the DMX bus. This function blocks indefinitely
 * until the DMX driver is idle and then sends a packet.
 *
 * @note This function uses FreeRTOS direct-to-task notifications to block and
 * unblock. Using task notifications on the same task that calls this function
 * can lead to undesired behavior and program instability.
 *
 * @param dmx_num The DMX port number.
 * @param size The size of the packet to send. If 0, sends the number of bytes
 * equal to the highest slot number that was written or sent in the previous
 * call to dmx_write(), dmx_write_offset(), dmx_write_slot(), or dmx_send().
 * @return The number of bytes sent on the DMX bus.
 */
size_t dmx_send(dmx_port_t dmx_num, size_t size);

/**
 * @brief Enables the DMX sniffer to determine the DMX break and
 * mark-after-break length.
 *
 * @note The sniffer uses the default GPIO ISR handler, which allows for many
 * ISRs to be registered to different GPIO pins. Depending on how many GPIO
 * interrupts are registered, there could be significant latency between when
 * the analyzer ISR runs and when an ISR condition actually occurs. A quirk of
 * this implementation is that ISRs are handled from lowest GPIO number to
 * highest. It is therefore recommended that the user shorts the UART rx pin to
 * the lowest numbered GPIO possible and enables the sniffer interrupt on that
 * pin to ensure that the analyzer ISR is called with the lowest latency
 * possible.
 *
 * @param dmx_num The DMX port number.
 * @param intr_pin The pin to which to assign the interrupt.
 * @return true on success.
 * @return false on failure.
 */
bool dmx_sniffer_enable(dmx_port_t dmx_num, int intr_pin);

/**
 * @brief Disables the DMX sniffer.
 *
 * @param dmx_num The DMX port number.
 * @return true on success.
 * @return false on failure.
 */
bool dmx_sniffer_disable(dmx_port_t dmx_num);

#ifdef __cplusplus
}
#endif
