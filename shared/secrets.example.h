// shared/secrets.example.h — template for shared/secrets.h.
//
//   cp shared/secrets.example.h shared/secrets.h
//
// then fill in your values. shared/secrets.h is gitignored; never commit it.
#pragma once

#define PEEKBOX_WIFI_SSID     "your-wifi-ssid"
#define PEEKBOX_WIFI_PASS     "your-wifi-password"

// Telegram: create a bot with @BotFather to get the token, then message the bot
// once and read your chat id from https://api.telegram.org/bot<TOKEN>/getUpdates
// Leave the token empty to disable Telegram.
#define PEEKBOX_TG_BOT_TOKEN  ""
#define PEEKBOX_TG_CHAT_ID    ""
