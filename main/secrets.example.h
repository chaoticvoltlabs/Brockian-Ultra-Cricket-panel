/**
 * @file  secrets.example.h
 * @brief Local Wi-Fi and BUC endpoint template.
 *
 * Copy this file to `main/secrets.h` and fill in values for your environment.
 * The real `main/secrets.h` is intentionally ignored by git.
 */

#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define BUC_HOST        "192.0.2.10"
#define BUC_PORT        80
#define BUC_API_TOKEN   "OPTIONAL_TOKEN_PLACEHOLDER"
#define BUC_API_END_POINT "/api/panel/weather"

#endif /* SECRETS_H */
