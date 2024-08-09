/**
 * @file spi_lib.h
 * @brief Header file for SPI communication library.
 *
 * This file provides the interface for SPI communication, including
 * initialization, data transmission, and response handling. It defines
 * data structures and function prototypes for interacting with SPI devices.
 */

#ifndef SPI_LIB_H
#define SPI_LIB_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Structure to hold the response data from SPI communication.
 */
typedef struct {
    uint8_t function_id;    /**< Function ID associated with the response */
    uint16_t payload_size;  /**< Size of the payload data in bytes */
    uint8_t *payload;       /**< Pointer to the payload data */
} spi_response_t;

/**
 * @brief Enumeration of possible error codes during SPI communication.
 */
typedef enum {
    SPI_SUCCESS,              /**< Operation completed successfully */
    SPI_ERROR_INVALID_FORMAT, /**< Error due to invalid message format */
    SPI_ERROR_CRC_MISMATCH,   /**< CRC mismatch error */
    SPI_ERROR_UNKNOWN         /**< Unknown error */
} spi_error_t;

/**
 * @brief Type definition for the callback function used in SPI communication.
 *
 * @param error Error code indicating the result of the operation.
 * @param response Pointer to the response data structure.
 */
typedef void (*spi_callback_t)(spi_error_t error, spi_response_t *response);

/**
 * @brief Binds the spidev driver to the specified SPI device.
 *
 * This function is used to bind the spidev driver to the SPI device,
 * allowing communication with it through user-space interfaces.
 */
void bind_spidev_driver(void);

/**
 * @brief Initializes the SPI device.
 *
 * This function sets up the SPI device with the appropriate settings
 * such as mode, bits per word, and speed. It must be called before
 * any other SPI operations.
 */
void spi_init(void);

/**
 * @brief Closes the SPI device.
 *
 * This function closes the SPI device, releasing any resources
 * that were allocated during initialization.
 */
void spi_close(void);

/**
 * @brief Initializes the GPIO for interrupt signaling.
 *
 * This function sets up the GPIO pin used for signaling interrupts
 * from the SPI slave device. It configures the pin for rising edge
 * events.
 */
void gpio_init(void);

/**
 * @brief Releases the GPIO resources.
 *
 * This function releases the GPIO line and closes the GPIO chip,
 * cleaning up resources used for GPIO operations.
 */
void gpio_close(void);

/**
 * @brief Sends a request to the SPI slave device.
 *
 * This function constructs a request message, including the function ID,
 * payload, CRC, and stop identifier. It sends the message via SPI and
 * waits for a response through a GPIO interrupt.
 *
 * @param function_id The function ID for the request.
 * @param payload Pointer to the payload data to be sent.
 * @param payload_size Size of the payload data in bytes.
 * @param callback Callback function to handle the response.
 */
void send_request(uint8_t function_id, const uint8_t *payload, uint16_t payload_size, spi_callback_t callback);

/**
 * @brief Starts receiving data from the SPI slave device.
 *
 * This function enables continuous reception of data from the SPI slave
 * device. It is useful for scenarios where the SPI slave initiates communication
 * without a preceding request from the master.
 *
 * @param callback Callback function to handle the incoming data.
 */
void start_receiving(spi_callback_t callback);

#endif // SPI_LIB_H
