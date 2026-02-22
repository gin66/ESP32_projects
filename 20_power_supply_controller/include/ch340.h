#pragma once
#include <USB.h>

// CH340 USB-serial driver for USB Host Shield 2.0 (MAX3421E).
// Supports QinHeng CH340/CH341 USB-serial chips, commonly found in Chinese
// lab power supplies, Arduino clones, and USB-TTL adapters.

#define CH340_VID  0x1A86  // QinHeng Electronics
// Supported PIDs:
//   0x7523  CH340G  (most common, used in HM310T)
//   0x5523  CH340T
//   0x7522  CH341A  (serial mode)
//   0x55D4  CH341   (newer variant)

#define CH340_N_EP 3   // EP0 control + EP1 bulk-IN + EP2 bulk-OUT

class CH340 : public USBDeviceConfig {
 public:
  explicit CH340(USB *p);

  // USBDeviceConfig interface
  bool    VIDPIDOK(uint16_t vid, uint16_t pid) override;
  uint8_t Init(uint8_t parent, uint8_t port, bool lowspeed) override;
  uint8_t Release() override;
  uint8_t Poll() override { return 0; }
  uint8_t GetAddress() override { return bAddress; }

  bool connected() const { return bAddress != 0; }

  // Non-blocking serial I/O
  uint8_t write(const uint8_t *buf, uint16_t len);
  // Returns 0 on success or no-data (hrNAK). *actual == 0 means no data yet.
  uint8_t read(uint8_t *buf, uint16_t maxlen, uint16_t *actual);

 private:
  USB    *pUsb;
  uint8_t bAddress;
  EpInfo  epInfo[CH340_N_EP];

  uint8_t vendorWrite(uint8_t req, uint16_t wVal, uint16_t wIdx);
  uint8_t vendorRead(uint8_t req, uint16_t wVal, uint16_t wIdx,
                     uint8_t *buf, uint16_t len);
  uint8_t serialInit(uint32_t baud);
};
