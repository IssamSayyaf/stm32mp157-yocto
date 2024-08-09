#include "spi_lib.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>
#include <poll.h>
#include <errno.h>
#include <stdarg.h> // Include for va_start and va_end

#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_MODE SPI_MODE_0
#define SPI_BITS_PER_WORD 8
#define SPI_SPEED 500000U
#define GPIO_CHIP "/dev/gpiochip3"
#define GPIO_PIN 15  // GPIO pin for interrupt signaling
#define DEBUG 0
#define CONSUMER "SPI_Consumer"

#define START_IDENTIFIER_SIZE 2
#define STOP_IDENTIFIER_SIZE 2
#define MAX_PAYLOAD_SIZE 1024
#define MESSAGE_SIZE (START_IDENTIFIER_SIZE + 1 + 2 + MAX_PAYLOAD_SIZE + 1 + STOP_IDENTIFIER_SIZE)
#define SIZE_CRC8   1

const uint8_t START_IDENTIFIER[START_IDENTIFIER_SIZE] = {0x48, 0x5A};
const uint8_t STOP_IDENTIFIER[STOP_IDENTIFIER_SIZE] = {0x0D, 0x0A};

typedef struct {
    uint8_t start_identifier[START_IDENTIFIER_SIZE];
    uint8_t function_id;
    uint8_t payload_size[2];
    uint8_t payload[];  // Flexible array member
    // uint8_t crc8;    // Will be placed after payload dynamically
    // uint8_t stop_identifier[STOP_IDENTIFIER_SIZE];  // Same as above
} spi_message_t;

// CRC32 constants
#define CRC32_POLYNOMIAL 0x04C11DB7U
#define CRC32_INITIAL_VALUE 0xFFFFFFFFU
#define CRC32_FINAL_XOR_VALUE 0x00000000U

static int spi_fd;
static struct gpiod_line *gpio_line;
static struct gpiod_chip *gpio_chip;
static uint8_t response_buffer[MESSAGE_SIZE];

// Precomputed CRC32 table for fast CRC calculations
static uint32_t crc32_table[256];

/**
 * @brief Prints a formatted debug message if DEBUG is enabled.
 *
 * @param format The format string (as in printf).
 * @param ... Additional arguments for the format string.
 */
static void debug_print(const char* format, ...) {
    if (DEBUG == 1) {
        va_list args;
        va_start(args, format);
        (void)vprintf(format, args);
        va_end(args);
    }
}

/**
 * @brief Initializes the CRC32 lookup table.
 *
 * This function precomputes the CRC32 lookup table for efficient CRC calculation.
 */
static void crc32_init_table(void) {
    uint32_t remainder;
    uint32_t dividend;
    uint8_t bit;

    for (dividend = 0U; dividend < 256U; dividend++) {
        remainder = dividend << 24U;
        for (bit = 8U; bit > 0U; bit--) {
            if ((remainder & 0x80000000U) != 0U) {
                remainder = (remainder << 1U) ^ CRC32_POLYNOMIAL;
            } else {
                remainder = (remainder << 1U);
            }
        }
        crc32_table[dividend] = remainder;
    }
}

/**
 * @brief Calculates the CRC32 checksum for a given data array.
 *
 * @param data The data array to calculate the CRC for.
 * @param length The length of the data array.
 * @return The CRC32 checksum.
 */
uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = CRC32_INITIAL_VALUE;
    size_t byte;
    uint8_t index;

    for (byte = 0U; byte < length; byte++) {
        index = (uint8_t)((crc >> 24U) ^ data[byte]);
        crc = (crc32_table[index] ^ (crc << 8U));
    }

    return crc ^ CRC32_FINAL_XOR_VALUE;
}

/**
 * @brief Returns the least significant byte (LSB) of the CRC32 checksum.
 *
 * @param data The data array to calculate the CRC for.
 * @param length The length of the data array.
 * @return The LSB of the CRC32 checksum.
 */
uint8_t get_crc32_lsb_byte(const uint8_t *data, size_t length) {
    uint32_t crc32_value = calculate_crc32(data, length);
    return (uint8_t)(crc32_value & 0xFF);
}

/**
 * @brief Binds the spidev driver to the SPI device.
 *
 * This function sets the spidev driver for the specified SPI device.
 */
void bind_spidev_driver(void) {
    const char *spi_device_path = "/sys/devices/platform/soc/44005000.spi/spi_master/spi0/spi0.0";
    const char *driver_override_path = "/sys/bus/spi/drivers/spidev/bind";

    FILE *fp;

    char driver_override_file[256];
    (void)snprintf(driver_override_file, sizeof(driver_override_file), "%s/driver_override", spi_device_path);
    fp = fopen(driver_override_file, "w");
    if (fp == NULL) {
        perror("Failed to open driver_override file");
        exit(EXIT_FAILURE);
    }
    (void)fprintf(fp, "spidev");
    (void)fclose(fp);
    debug_print("Driver override set to spidev\n");

    fp = fopen(driver_override_path, "w");
    if (fp == NULL) {
        perror("Failed to open bind file");
        exit(EXIT_FAILURE);
    }
    (void)fprintf(fp, "spi0.0");
    (void)fclose(fp);
    debug_print("SPI device spi0.0 bound to spidev driver\n");
}

/**
 * @brief Initializes the SPI device.
 *
 * This function configures the SPI device with the specified mode, bits per word, and speed.
 */
void spi_init(void) {
    int ret;
    uint8_t mode = SPI_MODE;
    uint8_t bits = SPI_BITS_PER_WORD;
    uint32_t speed = SPI_SPEED;

    // Initialize the CRC32 table
    crc32_init_table();

    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        perror("Failed to open SPI device");
        exit(EXIT_FAILURE);
    }
    debug_print("SPI device opened: %s\n", SPI_DEVICE);

    ret = ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    if (ret < 0) {
        perror("Failed to set SPI mode");
        (void)close(spi_fd);
        exit(EXIT_FAILURE);
    }
    debug_print("SPI mode set to %d\n", (int)mode);

    ret = ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret < 0) {
        perror("Failed to set bits per word");
        (void)close(spi_fd);
        exit(EXIT_FAILURE);
    }
    debug_print("SPI bits per word set to %d\n", (int)bits);

    ret = ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret < 0) {
        perror("Failed to set SPI speed");
        (void)close(spi_fd);
        exit(EXIT_FAILURE);
    }
    debug_print("SPI speed set to %u Hz\n", speed);
}

/**
 * @brief Closes the SPI device.
 *
 * This function closes the file descriptor for the SPI device.
 */
void spi_close(void) {
    (void)close(spi_fd);
    debug_print("SPI device closed\n");
}

/**
 * @brief Initializes the GPIO for interrupt signaling.
 *
 * This function sets up the specified GPIO pin for detecting rising edge interrupts.
 */
void gpio_init(void) {
    gpio_chip = gpiod_chip_open(GPIO_CHIP);
    if (gpio_chip == NULL) {
        perror("Failed to open GPIO chip");
        exit(EXIT_FAILURE);
    }

    gpio_line = gpiod_chip_get_line(gpio_chip, GPIO_PIN);
    if (gpio_line == NULL) {
        perror("Failed to get GPIO line");
        gpiod_chip_close(gpio_chip);
        exit(EXIT_FAILURE);
    }

    if (gpiod_line_request_rising_edge_events(gpio_line, CONSUMER) < 0) {
        perror("Failed to request GPIO line as interrupt");
        gpiod_chip_close(gpio_chip);
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Releases the GPIO resources.
 *
 * This function releases the GPIO line and closes the GPIO chip.
 */
void gpio_close(void) {
    gpiod_line_release(gpio_line);
    gpiod_chip_close(gpio_chip);
}

/**
 * @brief Prints the received data in hexadecimal format.
 *
 * @param data The received data array.
 * @param length The length of the data array.
 */
static void print_received_data(const uint8_t *data, size_t length) {
    debug_print("Received data: ");
    for (size_t i = 0; i < length; i++) {
        debug_print("%02X ", data[i]);
    }
    debug_print("\n");
}

/**
 * @brief Processes the received response from the SPI slave.
 *
 * This function validates the response format, checks the CRC, and
 * invokes the callback with the parsed data or an error code.
 *
 * @param response The received response array.
 * @param length The length of the response array.
 * @param callback The callback function to handle the response.
 */
static void process_response(const uint8_t *response, size_t length, spi_callback_t callback) {
    debug_print("Processing response...\n");
    debug_print("Received response length: %zu\n", length);

    // Print the received data
    print_received_data(response, length);

    // Check for basic format validity
    if (length < START_IDENTIFIER_SIZE + 1 + 2 + 1 + STOP_IDENTIFIER_SIZE) { // minimum length
        debug_print("Error: Response length too short (%zu < 8)\n", length);
        callback(SPI_ERROR_INVALID_FORMAT, NULL);
        return;
    }

    if (memcmp(response, START_IDENTIFIER, START_IDENTIFIER_SIZE) != 0) {
        debug_print("Error: Invalid start identifier\n");
        callback(SPI_ERROR_INVALID_FORMAT, NULL);
        return;
    }

    uint8_t function_id = response[2];
    uint16_t payload_size = (uint16_t)((response[4] << 8U) | response[3]);
    debug_print("Function ID: %02X, Payload size: %u\n", function_id, payload_size);

    if (payload_size > MAX_PAYLOAD_SIZE) {
        debug_print("Error: Payload size exceeds maximum (%u > %u)\n", payload_size, MAX_PAYLOAD_SIZE);
        callback(SPI_ERROR_INVALID_FORMAT, NULL);
        return;
    }

    

    // Check the CRC and Stop Identifier
    uint8_t received_crc = response[5U + payload_size];
    uint8_t calculated_crc = get_crc32_lsb_byte(&response[5U], payload_size);
    debug_print("Received CRC: %02X, Calculated CRC: %02X\n", received_crc, calculated_crc);

    if (received_crc != calculated_crc) {
        debug_print("Error: CRC mismatch\n");
        callback(SPI_ERROR_CRC_MISMATCH, NULL);
        return;
    }

    // Correct position for stop identifier check
    size_t stop_identifier_position = 5U + payload_size + 1;
    if (memcmp(response + stop_identifier_position, STOP_IDENTIFIER, STOP_IDENTIFIER_SIZE) != 0) {
        debug_print("Error: Invalid stop identifier\n");
        callback(SPI_ERROR_INVALID_FORMAT, NULL);
        return;
    }

    // Valid response; prepare the response structure
    spi_response_t resp;
    resp.function_id = function_id;
    resp.payload_size = payload_size;
    resp.payload = (uint8_t *)&response[5U];

    debug_print("Valid response received. Function ID: %02X, Payload size: %u\n", function_id, payload_size);

    // Call the callback with the successful response
    callback(SPI_SUCCESS, &resp);
}

/**
 * @brief Waits for a GPIO interrupt and processes the SPI response.
 *
 * This non-blocking function waits for an interrupt on the specified GPIO line,
 * then performs an SPI read operation and processes the response.
 *
 * @param callback The callback function to handle the response.
 */
static void wait_for_gpio_interrupt(spi_callback_t callback) {
    struct pollfd pfd;
    int ret;

    pfd.fd = gpiod_line_event_get_fd(gpio_line);
    pfd.events = POLLIN;

    debug_print("Waiting for GPIO interrupt...\n");

    ret = poll(&pfd, 1, -1);  // Wait indefinitely for the GPIO interrupt
    if (ret > 0) {
        if ((pfd.revents & POLLIN) != 0) {
            struct gpiod_line_event event;
            ret = gpiod_line_event_read(gpio_line, &event);
            if ((ret == 0) && (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE)) {
                debug_print("GPIO interrupt detected\n");

                // Perform SPI read operation after the interrupt
                struct spi_ioc_transfer spi;
                (void)memset(&spi, 0, sizeof(spi));

                uint8_t dummy_tx[MESSAGE_SIZE] = {0xff}; // Dummy buffer for sending data
                spi.tx_buf = (unsigned long)dummy_tx;
                spi.rx_buf = (unsigned long)response_buffer;
                spi.len = MESSAGE_SIZE;
                spi.speed_hz = SPI_SPEED;
                spi.bits_per_word = SPI_BITS_PER_WORD;
                spi.delay_usecs = 0;

                ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &spi);
                if (ret < 0) {
                    perror("Failed to transfer SPI message");
                    callback(SPI_ERROR_UNKNOWN, NULL);
                } else {
                    // Process the response data
                    process_response(response_buffer, spi.len, callback);
                }
            }
        }
    } else {
        if (ret == -1) {
            perror("Poll error");
            callback(SPI_ERROR_UNKNOWN, NULL);
        }
    }
}

/**
 * @brief Prints the details of a SPI message.
 *
 * This function prints the details of a SPI message, including the start identifier,
 * function ID, payload size, payload data, CRC, and stop identifier.
 *
 * @param message The SPI message to print.
 * @param total_size The total size of the message.
 */
void print_message_details(const spi_message_t *message, size_t total_size) {
    debug_print("Message Details:\n");
    debug_print("Start Identifier: ");
    for (size_t i = 0; i < START_IDENTIFIER_SIZE; i++) {
        debug_print("%02X ", message->start_identifier[i]);
    }
    debug_print("\nFunction ID: %02X\n", message->function_id);
    debug_print("Payload Size: %02X %02X\n", message->payload_size[0], message->payload_size[1]);
    
    // Calculate actual payload size from the payload_size field
    uint16_t actual_payload_size = message->payload_size[0] | (message->payload_size[1] << 8);
    debug_print("Payload: ");
    for (size_t i = 0; i < actual_payload_size; i++) {
        debug_print("%02X ", message->payload[i]);
    }
    
    // CRC and stop identifier are positioned immediately after the payload
    uint8_t crc8 = *(uint8_t *)((uint8_t *)message + offsetof(spi_message_t, payload) + actual_payload_size);
    uint8_t *stop_identifier = (uint8_t *)message + offsetof(spi_message_t, payload) + actual_payload_size + 1;
    
    debug_print("\nCRC8: %02X\n", crc8);
    debug_print("Stop Identifier: ");
    for (size_t i = 0; i < STOP_IDENTIFIER_SIZE; i++) {
        debug_print("%02X ", stop_identifier[i]);
    }
    debug_print("\nTotal Size: %zu bytes\n", total_size);
}

/**
 * @brief Prints a message in hexadecimal format.
 *
 * This function prints a byte array in hexadecimal format, useful for debugging.
 *
 * @param message The byte array to print.
 * @param length The length of the byte array.
 */
void print_message_hex(const uint8_t *message, size_t length) {
    debug_print("Message to send: ");
    for (size_t i = 0; i < length; i++) {
        debug_print("%02X ", message[i]);
    }
    debug_print("\n");
}

/**
 * @brief Sends a request to the SPI slave.
 *
 * This function constructs a SPI message with the specified function ID and payload,
 * calculates the CRC, and sends the message via SPI.
 *
 * @param function_id The function ID for the request.
 * @param payload The payload data to send.
 * @param actual_payload_size The size of the payload data.
 * @param callback The callback function to handle the response.
 */
void send_request(uint8_t function_id, const uint8_t *payload, uint16_t actual_payload_size, spi_callback_t callback) {
    // Calculate the total size needed for the message
    size_t total_size = offsetof(spi_message_t, payload) + actual_payload_size + SIZE_CRC8 + STOP_IDENTIFIER_SIZE;
    uint8_t message_buffer[total_size]; // Dynamic allocation on stack based on total size

    // Cast the buffer to the spi_message_t struct
    spi_message_t *message = (spi_message_t *)message_buffer;

    // Initialize the message fields
    memcpy(message->start_identifier, START_IDENTIFIER, START_IDENTIFIER_SIZE);
    message->function_id = function_id;

    // Set payload size in little-endian format
    message->payload_size[0] = (uint8_t)(actual_payload_size & 0xFF);
    message->payload_size[1] = (uint8_t)((actual_payload_size >> 8) & 0xFF);

    // Copy the actual payload data
    memcpy(message->payload, payload, actual_payload_size);

    // Calculate CRC for the payload and set it after the payload
    uint8_t *crc_position = message_buffer + offsetof(spi_message_t, payload) + actual_payload_size;
    *crc_position = get_crc32_lsb_byte(payload, actual_payload_size);

    // Set the stop identifier after the CRC
    memcpy(crc_position + 1, STOP_IDENTIFIER, STOP_IDENTIFIER_SIZE);

    // Recalculate total size
    total_size = (crc_position + 1 + STOP_IDENTIFIER_SIZE) - message_buffer;

    // Print the detailed message for debugging
    debug_print("Message Details:\n");
    print_message_details(message, total_size);

    // Print the entire message as it will be sent
    debug_print("Message to send (entire buffer including stop identifier):\n");
    print_message_hex(message_buffer, total_size);

    // Debug print to show individual bytes in the buffer
    debug_print("Buffer contents before sending:\n");
    for (size_t i = 0; i < total_size; i++) {
        debug_print("Byte %zu: %02X\n", i, message_buffer[i]);
    }

    // Send the message via SPI
    struct spi_ioc_transfer spi;
    memset(&spi, 0, sizeof(spi));
    spi.tx_buf = (unsigned long)message_buffer;
    spi.rx_buf = (unsigned long)response_buffer;
    spi.len = total_size;
    spi.speed_hz = SPI_SPEED;
    spi.bits_per_word = SPI_BITS_PER_WORD;
    spi.delay_usecs = 0;

    debug_print("Starting SPI transfer with total size: %zu\n", total_size);
    int ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &spi);
    if (ret < 0) {
        perror("Failed to transfer SPI message");
        callback(SPI_ERROR_UNKNOWN, NULL);
        return;
    } else {
        debug_print("SPI transfer completed successfully, bytes transferred: %d\n", ret);
    }

    // Wait for interrupt and handle response asynchronously
    wait_for_gpio_interrupt(callback);
}
