/**
 * @file gpio.h
 * @author Mitch Weisbrod
 * @brief This file is the GPIO Hardware Abstraction Layer (HAL) of esp_dmx. It
 * contains low-level functions to perform tasks relating to the GPIO hardware.
 * GPIO is needed for the DMX sniffer. This file is not considered part of the
 * API and should not be included by the user.
 */
#pragma once

#include "dmx/hal.h"
#include "dmx_types.h"
#include "driver/gpio.h"
#include "rdm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Evaluates to true if the pin number used for TX is valid.
 */
#define dmx_tx_pin_is_valid(tx) ((tx) < 0 || GPIO_IS_VALID_OUTPUT_GPIO(tx))

/**
 * @brief Evaluates to true if the pin number used for RX is valid.
 */
#define dmx_rx_pin_is_valid(rx) ((rx) < 0 || GPIO_IS_VALID_GPIO(rx))

/**
 * @brief Evaluates to true if the pin number used for RTS is valid.
 */
#define dmx_rts_pin_is_valid(rts) ((rts) < 0 || GPIO_IS_VALID_OUTPUT_GPIO(rts))

/**
 * @brief Evaluates to true if the pin number used for the DMX sniffer is valid.
 */
#define dmx_sniffer_pin_is_valid(sniffer) (GPIO_IS_VALID_GPIO(sniffer))

/**
 * @brief A handle to the DMX GPIO.
 */
typedef struct dmx_gpio_t *dmx_gpio_handle_t;

/**
 * @brief Initializes the GPIO for the DMX sniffer.
 *
 * @param dmx_num The DMX port number.
 * @param[in] isr_handle The ISR function to be called the sniffer pin
 * transitions on any edge.
 * @param[in] isr_context Context to be used in the DMX GPIO ISR.
 * @param sniffer_pin The sniffer pin GPIO number.
 * @return A handle to the DMX GPIO or null on failure.
 */
dmx_gpio_handle_t dmx_gpio_init(dmx_port_t dmx_num, void *isr_handle,
                                void *isr_context, int sniffer_pin);

/**
 * @brief De-initializes the GPIO for the DMX sniffer.
 *
 * @param gpio A handle to the DMX GPIO.
 */
void dmx_gpio_deinit(dmx_gpio_handle_t gpio);

/**
 * @brief Reads the level of the DMX sniffer GPIO.
 *
 * @param gpio A handle to the DMX GPIO.
 * @return The level of the DMX GPIO.
 */
int dmx_gpio_read(dmx_gpio_handle_t gpio);

#ifdef __cplusplus
}
#endif
