#ifndef MURAL_ESP8266_PORTALHTML_H
#define MURAL_ESP8266_PORTALHTML_H

#include <WiFiManager.h>

// Applies the Mural dark theme and captive portal layout to WiFiManager
void applyPortalTheme(WiFiManager& wifiManager);

// Confirmation page returned by POST /offline in the captive portal
extern const char OFFLINE_PAGE_HTML[];

#endif // MURAL_ESP8266_PORTALHTML_H
