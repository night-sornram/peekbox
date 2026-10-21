#pragma once
#include <stddef.h>
#include <stdint.h>

// True when both the bot token and chat id are set in secrets.h.
bool telegramEnabled();

// Blocking upload (typically 1-3 s). Returns true on HTTP 200.
bool telegramSendPhoto(const uint8_t *jpeg, size_t len, const char *caption);
