#include "ch340.h"

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CH340::CH340(USB *p) : pUsb(p), bAddress(0) {
  memset(epInfo, 0, sizeof(epInfo));
  epInfo[0].maxPktSize = 8;
  epInfo[0].bmNakPower = USB_NAK_MAX_POWER;
  if (pUsb) pUsb->RegisterDeviceClass(this);
}

// ---------------------------------------------------------------------------
// VID/PID matching — called by UHS2 during enumeration
// ---------------------------------------------------------------------------

bool CH340::VIDPIDOK(uint16_t vid, uint16_t pid) {
  if (vid != CH340_VID) return false;
  // 0x7523 CH340G, 0x5523 CH340T, 0x7522 CH341A (serial mode), 0x55D4 CH341
  return (pid == 0x7523 || pid == 0x5523 || pid == 0x7522 || pid == 0x55D4);
}

// ---------------------------------------------------------------------------
// Init — called by UHS2 after VIDPIDOK returns true
// Follows the pattern of ftdi.cpp from USB Host Shield 2.0.
// ---------------------------------------------------------------------------

uint8_t CH340::Init(uint8_t parent, uint8_t port, bool lowspeed) {
  uint8_t buf[sizeof(USB_DEVICE_DESCRIPTOR)];
  auto *udd = reinterpret_cast<USB_DEVICE_DESCRIPTOR *>(buf);
  uint8_t rcode;
  EpInfo *oldep_ptr = NULL;
  AddressPool &addrPool = pUsb->GetAddressPool();

  if (bAddress) return USB_ERROR_CLASS_INSTANCE_ALREADY_IN_USE;

  UsbDevice *p = addrPool.GetUsbDevicePtr(0);
  if (!p || !p->epinfo) return USB_ERROR_ADDRESS_NOT_FOUND_IN_POOL;

  // Temporarily point device at our epInfo so EP0 control transfers work
  oldep_ptr  = p->epinfo;
  p->epinfo  = epInfo;
  p->lowspeed = lowspeed;

  rcode = pUsb->getDevDescr(0, 0, sizeof(USB_DEVICE_DESCRIPTOR), buf);
  if (rcode) { p->epinfo = oldep_ptr; return rcode; }

  epInfo[0].maxPktSize = udd->bMaxPacketSize0;

  bAddress = addrPool.AllocAddress(parent, false, port);
  if (!bAddress) return USB_ERROR_OUT_OF_ADDRESS_SPACE_IN_POOL;

  rcode = pUsb->setAddr(0, 0, bAddress);
  if (rcode) {
    addrPool.FreeAddress(bAddress);
    bAddress = 0;
    p->epinfo = oldep_ptr;
    return rcode;
  }

  p->epinfo = oldep_ptr;

  // CH340G bulk endpoints are fixed: IN=0x01, OUT=0x02, pktsize=32
  epInfo[1].epAddr     = 0x01;
  epInfo[1].maxPktSize = 0x20;
  epInfo[1].bmNakPower = USB_NAK_NOWAIT;
  epInfo[2].epAddr     = 0x02;
  epInfo[2].maxPktSize = 0x20;
  epInfo[2].bmNakPower = USB_NAK_NOWAIT;

  p = addrPool.GetUsbDevicePtr(bAddress);
  if (!p) return USB_ERROR_ADDRESS_NOT_FOUND_IN_POOL;
  p->lowspeed = lowspeed;

  rcode = pUsb->setEpInfoEntry(bAddress, CH340_N_EP, epInfo);
  if (rcode) { Release(); return rcode; }

  rcode = pUsb->setConf(bAddress, 0, 1);
  if (rcode) { Release(); return rcode; }

  rcode = serialInit(9600);
  if (rcode) { Release(); return rcode; }

  Serial.println("CH340: ready at 9600 8N1");
  return 0;
}

uint8_t CH340::Release() {
  pUsb->GetAddressPool().FreeAddress(bAddress);
  bAddress = 0;
  return 0;
}

// ---------------------------------------------------------------------------
// Vendor control helpers
// ---------------------------------------------------------------------------

uint8_t CH340::vendorWrite(uint8_t req, uint16_t wVal, uint16_t wIdx) {
  return pUsb->ctrlReq(bAddress, 0,
                       0x40, req,
                       wVal & 0xFF, wVal >> 8,
                       wIdx, 0, 0, NULL, NULL);
}

uint8_t CH340::vendorRead(uint8_t req, uint16_t wVal, uint16_t wIdx,
                          uint8_t *buf, uint16_t len) {
  return pUsb->ctrlReq(bAddress, 0,
                       0xC0, req,
                       wVal & 0xFF, wVal >> 8,
                       wIdx, len, len, buf, NULL);
}

// ---------------------------------------------------------------------------
// CH340 serial port initialisation (baud rate formula from Linux ch341.c)
// ---------------------------------------------------------------------------

uint8_t CH340::serialInit(uint32_t baud) {
  uint8_t ver[2] = {};
  vendorRead(0x5F, 0, 0, ver, 2);
  Serial.printf("CH340 version: 0x%02X 0x%02X\n", ver[0], ver[1]);

  // Compute baud divisor
  uint32_t factor  = 1532620800UL / baud;
  uint8_t  divisor = 3;
  while (factor > 0xfff0 && divisor > 0) { factor >>= 3; divisor--; }
  if (factor > 0xfff0) return USB_ERROR_UNSUPPORTED_FEATURE;

  // wVal encodes the prescaler; wIdx is the LCR (8N1)
  uint16_t wVal = ((uint16_t)(0x10000UL - factor) & 0xFF00) | divisor;
  uint16_t wIdx = 0x00C3;  // ENABLE_RX | ENABLE_TX | CS8

  uint8_t r = vendorWrite(0x9A, wVal, wIdx);
  if (r) return r;

  // Assert DTR + RTS (active-low control bits, ~(DTR|RTS) & 0xFF = 0x9F)
  return vendorWrite(0xA4, 0x009F, 0);
}

// ---------------------------------------------------------------------------
// Bulk I/O
// ---------------------------------------------------------------------------

uint8_t CH340::write(const uint8_t *buf, uint16_t len) {
  return pUsb->outTransfer(bAddress, epInfo[2].epAddr, len, (uint8_t *)buf);
}

uint8_t CH340::read(uint8_t *buf, uint16_t maxlen, uint16_t *actual) {
  *actual = maxlen;
  uint8_t r = pUsb->inTransfer(bAddress, epInfo[1].epAddr, actual, buf);
  if (r == hrNAK) { *actual = 0; return 0; }  // no data yet — not an error
  return r;
}
