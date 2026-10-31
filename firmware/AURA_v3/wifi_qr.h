// wifi_qr.h - Wi-Fi join QR for the setup portal (Q01). Uses the vendored
// Nayuki qrcodegen (src/qrcodegen, MIT). Pure code: host-testable.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define WQR_MAX_VER 6  // 41x41; "WIFI:T:WPA;S:AURA-Setup-XXXX;P:xxxx-xxxx;;" fits in version 3 (ECC M+)
#define WQR_BUF_LEN (((WQR_MAX_VER * 4 + 17) * (WQR_MAX_VER * 4 + 17) + 7) / 8 + 1)

// The ONLY place the join-QR payload is built:
//   WIFI:T:WPA;S:<ssid>;P:<password>;;   (\ ; , : " escaped with a backslash)
// Returns false if it does not fit in cap.
bool wifi_qr_text(char *out, size_t cap, const char *ssid, const char *pass);
// Encodes the payload into qr[WQR_BUF_LEN] (ECC boosted from LOW). Returns the
// side length in modules, or 0 on failure. The temporary payload is wiped.
int wifi_qr_encode(const char *ssid, const char *pass, uint8_t *qr);
bool wifi_qr_module(const uint8_t *qr, int x, int y);
