# White Paper: nRF24 Temperature Broadcast System

## 1. System Overview

This system establishes a wireless temperature monitoring link between two ATmega328p
Arduino Nano boards connected via nRF24L01+ 2.4 GHz transceiver modules. One board
(**01_heizung_temp**) acts as a **transmitter (TX)** reading Dallas/Maxim DS18B20
temperature sensors, while the other (**02_nrf_receiver**) acts as a **receiver (RX)**
that relays received data to a host computer via USB serial.

### 1.1 Architecture

```
┌──────────────────────┐         ┌─────────────────────────┐
│  01_heizung_temp     │         │  02_nrf_receiver        │
│  ATmega328p Nano     │  nRF24  │  ATmega328p Nano        │
│                      │◄──────►│                         │
│  DS18B20 Sensor #1   │  2.4GHz │  USB Serial → Host PC   │
│  DS18B20 Sensor #2   │  Air    │  (monitor: 115200 baud) │
│  nRF24L01+ Radio     │         │  nRF24L01+ Radio        │
└──────────────────────┘         └─────────────────────────┘
```

### 1.2 Design Goals

| Goal | Detail |
|------|--------|
| **Reliability** | Simple point-to-point unidirectional link with ACK + retries |
| **Speed** | 250 kbps datarate (slowest = best receiver sensitivity/range) |
| **Error handling** | CRC-CCITT validation, error flags propagated TX→RX |
| **Scalability** | Broadcast model allows multiple TX nodes on same channel |
| **Debugability** | Full temperature values + error flags printed to serial |

## 2. Hardware

### 2.1 Microcontroller

- **Part**: ATmega328P (Arduino Nano form factor)
- **Clock**: 16 MHz internal/external crystal
- **Flash**: 32 KB (well within our needs)
- **SRAM**: 2 KB (sufficient for payload buffering)

### 2.2 Radio Module

- **Part**: nRF24L01+ integrated on emakefun RF-Nano board
- **Frequency**: 2.4 GHz ISM band
- **Board**: emakefun RF-Nano V1.0 (ATmega328P-MU, micro-USB)
- **Reference**: [emakefun/rf-nano on GitHub](https://github.com/emakefun/rf-nano)

### 2.3 Temperature Sensors

- **Part**: Maxim DS18B20 (or compatible DS1822, DS18B20-PAR)
- **Interface**: 1-Wire (single digital pin)
- **Resolution**: 9–12 bit configurable (12 bit = ~0.0625 °C)
- **Quantity**: 2 sensors on one 1-Wire bus (pin D4)

### 2.4 Pin Wiring

#### Integrated nRF24L01 on emakefun RF-Nano (Both Nodes)

Both boards use **emakefun RF-Nano V1.0** with integrated nRF24L01+.
Pin mapping for V1.0 differs from the standard RF24 library defaults.
Verified working pins: CE=10, CSN=9.

| nRF24 Pin | Arduino Nano Pin | Notes |
|-----------|-------------------|-------|
| GND | GND | Common ground |
| VCC | 3.3V | Onboard 3.3V rail |
| CE | D10 | Chip Enable (V1.0 spec) |
| CSN (CE_N) | D9 | Chip Select Not (V1.0 spec) |
| MOSI | D11 | SPI MOSI |
| MISO | D12 | SPI MISO |
| SCK | D13 | SPI Clock |

Both transmitter and receiver use identical pin mappings.

> **Note**: Standard RF-Nano V3.0 uses CE=D7, CSN=D8 (RF24 library default).
> Our V1.0 boards use CE=D10, CSN=D9 — the pin mapping is swapped.

#### Temperature Sensors

| DS18B20 Pin | Connection |
|-------------|------------|
| GND | GND |
| DQ (data) | Arduino D4 (via 4.7 kΩ pull-up to VCC) |
| VDD | 3.3V–5V (parasitic power also supported) |

Two DS18B20 sensors are connected in parallel on the same 1-Wire bus (D4).

## 3. Communication Protocol

### 3.1 Physical Layer

| Parameter | Value | Rationale |
|-----------|-------|------|
| Datarate | **250 kbps** (`RF24_250KBPS`) | Slowest rate = best receiver sensitivity and range |
| Frequency channel | Channel 80 (2540 MHz) | Clear of WiFi channels 1–11 |
| Output power | `RF24_PA_MAX` (0 dBm) | Maximum transmit power |
| Address width | 5 bytes | Standard |
| Payload size | 20 bytes | Fixed, supports up to 4 sensors |
| CRC | 16-bit | Automatic in hardware |

**Why 250 kbps?** The nRF24L01+ supports three datarates: 250 kbps, 1 Mbps, and 2 Mbps.
The 250 kbps rate provides the longest range and best receiver sensitivity (–89 dBm vs.
–80 dBm at 1/2 Mbps). This is critical for a temperature monitoring application where
the transmitter may be placed at some distance from the receiver.

> **Warning**: 250 kbps only works with nRF24L01+ (plus) modules. Standard nRF24L01
> (non-plus) modules will fail when 250 kbps is selected.

### 3.2 Payload Format (Binary)

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬───────────┬───────────┬───────────┐
│ Magic    │ Sensor 1 │ Sensor 2 │ Sensor 3 │ Sensor 4 │ OK Counter│ Fail Ctr  │ CRC16     │
│ (2 byte) │ (2 byte) │ (2 byte) │ (2 byte) │ (2 byte) │ (4 byte)  │ (4 byte)  │ (2 byte)  │
│ 0xAB 0xCD│ int16    │ int16    │ int16    │ int16    │ uint32    │ uint32    │ CRC-CCITT │
└──────────┴──────────┴──────────┴──────────┴──────────┴───────────┴───────────┴───────────┘
            ←── 20 bytes total ──→
```

| Field | Size | Type | Description |
|-------|------|------|-------------|
| Magic | 2 B | `uint16_t` | `0xABCD` — validates payload origin |
| Sensor 1 | 2 B | `int16_t` | Temperature 1 in c° (°C × 100), sorted by ROM |
| Sensor 2 | 2 B | `int16_t` | Temperature 2 in c° (°C × 100), sorted by ROM |
| Sensor 3 | 2 B | `int16_t` | Temperature 3 in c° (°C × 100), sorted by ROM |
| Sensor 4 | 2 B | `int16_t` | Temperature 4 in c° (°C × 100), sorted by ROM |
| OK Counter | 4 B | `uint32_t` | Consecutive successful transmission counter |
| Fail Counter | 4 B | `uint32_t` | Consecutive failed transmission counter |
| CRC16 | 2 B | `uint16_t` | CRC-CCITT (0xFFFF init) over all fields before CRC |

**Sensor identification**: Sensors are sorted by their 1-Wire ROM address on the TX side.
The positional slot (sensor1/sensor2/sensor3/sensor4) is stable and serves as the implicit
sensor ID — no explicit ID field is needed. When fewer than 4 sensors are connected,
unconnected slots carry the error sentinel `INT16_MIN` (-32768).

**Error handling**: Unconnected sensor slots carry `INT16_MIN`, which the receiver
recognizes as "no sensor" without needing separate error flags.

**Rationale for int16_t c°**: Integer-only representation avoids float precision loss on
AVR. The DS18B20 raw value (1/128 °C resolution) is converted to centidegrees (1/100 °C)
via integer arithmetic: `cdeg = whole × 100 + round(fraction × 100/128)`.

**Rationale for CRC16**: Provides error detection against radio corruption. The nRF24L01+
hardware CRC (16-bit) protects the payload in transit; software CRC validates the
reconstructed payload structure.

### 3.3 Broadcast Schedule

- **Interval**: 1 second (1000 ms)
- **Method**: Non-blocking `mill()`-based timer (no `delay()` blocking)
- **Mechanism**: TX opens a point-to-point pipe, writes payload with auto-ACK enabled.
  On failure, the radio retries up to 15 times (500µs delay between retries = ~15ms max).
  Failure count is included in the payload for receiver-side monitoring.

### 3.4 RF24 Configuration (Both Nodes)

```cpp
RF24 radio(10, 9);             // CE=D10, CSN=D9 (rf-nano V1.0 spec)
radio.begin();
radio.setChannel(80);          // 2540 MHz
radio.setDataRate(RF24_250KBPS); // 250 kbps (slowest, best range)
radio.setPALevel(RF24_PA_MAX); // 0 dBm (max power)
radio.setPayloadSize(20);      // 20 bytes (4 sensors)
radio.openWritingPipe(PIPE_ADDRESS);   // TX → RX pipe address
radio.openReadingPipe(1, PIPE_ADDRESS); // RX listens on same address
radio.setCRCLength(RF24_CRC_16);  // 16-bit CRC
radio.setAutoAck(true);         // ACK + retries enabled
radio.setRetries(5, 15);        // 500µs delay, 15 retries
radio.stopListening();          // TX mode (RX: startListening())
```

Receiver configuration is identical except `radio.startListening()` replaces
`stopListening()` to enter RX mode.

## 4. Implementation Plan

### Phase 1 — Transmitter (01_heizung_temp)

1. Retain existing DS18B20 sensor reading logic
2. Add RF24 radio initialization
3. Build payload struct with magic, two float temperatures, CRC
4. Broadcast payload every 1000 ms using non-blocking timer
5. Retain serial output for debugging

### Phase 2 — Receiver (02_nrf_receiver)

1. Retain existing heartbeat stub (replace with nRF logic)
2. Add RF24 radio initialization
3. Enter listening mode on same pipe address
4. Poll for available payload every 10 ms
5. Validate magic bytes and CRC
6. Extract and print sensor temperatures to serial

### Phase 3 — Integration & Testing

1. Flash both firmwares
2. Verify serial output on receiver matches transmitter readings
3. Test range and reliability
4. Iterate on timing and payload as needed

## 5. Test Results

| Metric | Result |
|--------|--------|
| Pin mapping | CE=D10, CSN=D9 (verified, V1.0 boards) |
| Datarate | 250 kbps |
| Packets received | 100% (zero losses over test period) |
| Magic validation | 100% OK |
| CRC validation | 100% OK |
| TX failures | 0 |
| RX failures | 0 |
| Temperature range | ~27.25–27.50°C (room temperature) |

## 6. Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------|
| Pin mapping mismatch (V1.0 vs V3.0) | Medium | Verified CE=D10, CSN=D9 for V1.0 boards |
| 3.3V power supply insufficient for radio | Low | RF-Nano has adequate 3.3V regulator |
| 1-Wire bus timing conflicts with RF24 SPI | Low | Both use different pins; RF24 uses hardware SPI |
| Radio interference on channel 80 | Low | Channel 80 is in a relatively clean part of 2.4 GHz band |
| Float precision on AVR | Low | AVR `float` is IEEE 754 single precision (4 B) |

## 7. Future Extensions

- **Multi-TX support**: Assign unique magic numbers per sensor node
- **Battery operation**: Add sleep modes between transmissions
- **Configuration**: Add a configuration command via serial to change channel/payload
- **Sensor count**: Payload supports up to 4 sensors (20 bytes). Additional sensors
would require more radio bandwidth.
- **Encryption**: Add simple XOR or AES-CCM for data confidentiality

## 8. References

- nRF24L01+ Product Specification: [Nordic Semiconductor](https://www.nordicsemi.com)
- RF24 Arduino Library: [maniacbug/RF24 on GitHub](https://github.com/maniacbug/RF24)
- DS18B20 Data Sheet: Maxim Integrated
- Arduino OneWire Library: Paul Stoffregen
- DallasTemperature Library: Miles Burton
- **emakefun RF-Nano**: [emakefun/rf-nano on GitHub](https://github.com/emakefun/rf-nano)
  - Pin mapping: CE=D7/D10, CSN=D8/D9 (depends on board version)
  - V3.0 (CE=D7, CSN=D8) is compatible with RF24 library defaults
  - V1.0/V2.0 (CE=D10, CSN=D9) requires custom pin specification
