// telegram.cpp — send a JPEG to a Telegram chat via the Bot API (sendPhoto, multipart upload).
#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "secrets.h"
#include "telegram.h"

// Root CA for api.telegram.org: "Go Daddy Root Certificate Authority - G2", valid until 2037-12-31.
// SHA-256 fingerprint 45:14:0B:32:47:EB:9C:C8:C5:B4:F0:D7:B5:30:91:F7:32:92:08:9E:6E:5A:63:E2:74:9D:D3:AC:A9:19:8E:DA
// Chain checked 2026-10-21: api.telegram.org <- Go Daddy Secure CA - G2 <- this root.
static const char TELEGRAM_ROOT_CA[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDxTCCAq2gAwIBAgIBADANBgkqhkiG9w0BAQsFADCBgzELMAkGA1UEBhMCVVMx
EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoT
EUdvRGFkZHkuY29tLCBJbmMuMTEwLwYDVQQDEyhHbyBEYWRkeSBSb290IENlcnRp
ZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5MDkwMTAwMDAwMFoXDTM3MTIzMTIz
NTk1OVowgYMxCzAJBgNVBAYTAlVTMRAwDgYDVQQIEwdBcml6b25hMRMwEQYDVQQH
EwpTY290dHNkYWxlMRowGAYDVQQKExFHb0RhZGR5LmNvbSwgSW5jLjExMC8GA1UE
AxMoR28gRGFkZHkgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIw
DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAL9xYgjx+lk09xvJGKP3gElY6SKD
E6bFIEMBO4Tx5oVJnyfq9oQbTqC023CYxzIBsQU+B07u9PpPL1kwIuerGVZr4oAH
/PMWdYA5UXvl+TW2dE6pjYIT5LY/qQOD+qK+ihVqf94Lw7YZFAXK6sOoBJQ7Rnwy
DfMAZiLIjWltNowRGLfTshxgtDj6AozO091GB94KPutdfMh8+7ArU6SSYmlRJQVh
GkSBjCypQ5Yj36w6gZoOKcUcqeldHraenjAKOc7xiID7S13MMuyFYkMlNAJWJwGR
tDtwKj9useiciAF9n9T521NtYJ2/LOdYq7hfRvzOxBsDPAnrSTFcaUaz4EcCAwEA
AaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYE
FDqahQcQZyi27/a9BUFuIMGU2g/eMA0GCSqGSIb3DQEBCwUAA4IBAQCZ21151fmX
WWcDYfF+OwYxdS2hII5PZYe096acvNjpL9DbWu7PdIxztDhC2gV7+AJ1uP2lsdeu
9tfeE8tTEH6KRtGX+rcuKxGrkLAngPnon1rpN5+r5N9ss4UXnT3ZJE95kTXWXwTr
gIOrmgIttRD02JDHBHNA7XIloKmf7J6raBKZV8aPEjoJpL1E/QYVN8Gb5DKj7Tjo
2GTzLH4U/ALqn83/B2gX2yKQOC16jdFU8WnjXzPKej17CuPKf1855eJ1usV2GDPO
LPAvTK33sefOT6jEm0pUBsV/fdUID+Ic/n4XuKxe9tQWskMJDE32p2u0mYRlynqI
4uJEvlz36hz1
-----END CERTIFICATE-----
)PEM";

static const char *HOST = "api.telegram.org";
static const char *BOUNDARY = "----PeekBoxBoundary7MA4YWxk";

bool telegramEnabled() {
  return strlen(PEEKBOX_TG_BOT_TOKEN) > 0 && strlen(PEEKBOX_TG_CHAT_ID) > 0;
}

bool telegramSendPhoto(const uint8_t *jpeg, size_t len, const char *caption) {
  if (!telegramEnabled()) return false;

  WiFiClientSecure client;
  client.setCACert(TELEGRAM_ROOT_CA);
  client.setTimeout(10);  // seconds
  if (!client.connect(HOST, 443)) {
    Serial.println("[tg] TLS connect to api.telegram.org failed");
    return false;
  }

  String head;
  head.reserve(384);
  head += "--"; head += BOUNDARY; head += "\r\n";
  head += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  head += PEEKBOX_TG_CHAT_ID; head += "\r\n";
  head += "--"; head += BOUNDARY; head += "\r\n";
  head += "Content-Disposition: form-data; name=\"caption\"\r\n\r\n";
  head += caption; head += "\r\n";
  head += "--"; head += BOUNDARY; head += "\r\n";
  head += "Content-Disposition: form-data; name=\"photo\"; filename=\"peekbox.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";
  String tail = String("\r\n--") + BOUNDARY + "--\r\n";

  client.printf("POST /bot%s/sendPhoto HTTP/1.1\r\n", PEEKBOX_TG_BOT_TOKEN);
  client.printf("Host: %s\r\n", HOST);
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY);
  client.printf("Content-Length: %u\r\n", (unsigned)(head.length() + len + tail.length()));
  client.print("Connection: close\r\n\r\n");
  client.print(head);

  // TLS records are small; feed the JPEG in chunks.
  for (size_t off = 0; off < len;) {
    size_t n = client.write(jpeg + off, min((size_t)1024, len - off));
    if (n == 0) {
      Serial.println("[tg] write failed");
      client.stop();
      return false;
    }
    off += n;
  }
  client.print(tail);

  String status = client.readStringUntil('\n');
  client.stop();
  bool ok = status.startsWith("HTTP/1.1 200");
  // Never log the request line: it contains the bot token.
  Serial.printf("[tg] sendPhoto -> %s\n", ok ? "ok" : status.c_str());
  return ok;
}
