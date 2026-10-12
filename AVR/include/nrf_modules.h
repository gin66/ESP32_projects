/**
 * @file nrf_modules.h
 * @brief Shared protocol structs and constants for AVR nRF24 projects
 *
 * This file is public — commit it to the repository.
 * Sensitive identifiers (pipe addresses, serial numbers) are in
 * $HOME/src/.private/private_haus.h and must NOT be committed.
 */

#ifndef NRF_MODULES_H
#define NRF_MODULES_H

#include <stdint.h>

/*==================================================
 * ESmart3 protocol constants
 *==================================================*/

#define COMMAND_HEAD_LEN  2

struct Esmart3Command {
    uint8_t counter;
    uint8_t length;
    uint8_t data[32 - COMMAND_HEAD_LEN];
};

#define PAYLOAD_HEAD_LEN  11

struct Esmart3Payload {
    uint8_t counter;
    uint8_t length;
    uint32_t    err_timeout_read_command;
    uint32_t    err_timeout_write_payload;
    uint8_t     data_offset;       /* MSB set for last message */
    uint8_t     data[32 - PAYLOAD_HEAD_LEN];
};

/*==================================================
 * Relay module protocol
 *==================================================*/

struct RelayCommand {
    uint8_t relay_old;
    uint8_t relay_new;
    uint8_t counter;
};

#define EMERGENCY_NRF24_TIMEOUT       1
#define EMERGENCY_READ_COMMAND_TIMEOUTS 2
#define EMERGENCY_WRITE_PAYLOAD_TIMEOUTS 4
#define EMERGENCY_START_ADC_TIMEOUTS    8
#define EMERGENCY_READ_ADC_TIMEOUTS    16

struct RelayPayload {
    int16_t  raw[4];
    uint8_t  counter;
    uint32_t conversions;
    uint8_t  emergency_shutdown_reason;
    uint16_t err_timeout_read_command;
    uint16_t err_timeout_write_payload;
    uint16_t err_timeout_start_adc;
    uint16_t err_timeout_read_adc;
};

#endif /* NRF_MODULES_H */
