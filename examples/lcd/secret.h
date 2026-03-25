#pragma once

#include <Utils.h>

#include "menu.h"
#include "MyMesh.h"

inline bool encodeBase64Psk16(char* dest, size_t destSize, const uint8_t* src) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (!dest || !src || destSize < 25) {
        return false;
    }

    size_t out = 0;
    for (int i = 0; i < 15; i += 3) {
        const uint32_t chunk = ((uint32_t) src[i] << 16) | ((uint32_t) src[i + 1] << 8) | src[i + 2];
        dest[out++] = alphabet[(chunk >> 18) & 0x3F];
        dest[out++] = alphabet[(chunk >> 12) & 0x3F];
        dest[out++] = alphabet[(chunk >> 6) & 0x3F];
        dest[out++] = alphabet[chunk & 0x3F];
    }

    const uint32_t tail = ((uint32_t) src[15] << 16);
    dest[out++] = alphabet[(tail >> 18) & 0x3F];
    dest[out++] = alphabet[(tail >> 12) & 0x3F];
    dest[out++] = '=';
    dest[out++] = '=';
    dest[out] = '\0';
    return true;
}

inline bool isHashtagChannelName(const char* channelName) {
    if (!channelName || channelName[0] != '#' || channelName[1] == '\0') {
        return false;
    }

    for (int i = 1; channelName[i] != '\0'; i++) {
        const char c = channelName[i];
        const bool isLowercaseLetter = (c >= 'a' && c <= 'z');
        const bool isDigit = (c >= '0' && c <= '9');
        if (!isLowercaseLetter && !isDigit) {
            return false;
        }
    }

    return true;
}

inline ChannelDetails* addSecretChannel(MyMesh* mesh, GUI* gui, Menu* menu, const char* channelName, const char* pskBase64) {
    ChannelDetails* details = mesh->addChannel(channelName, pskBase64);
    if (!details) return nullptr;

    mesh->channels.push_back(details);

    Channel* channel = new Channel(gui, details);
    MIPage* page = new MIPage(gui, channelName, channel);
    menu->add(page);
    return details;
}

inline bool generateHashtagChannelPskBase64(char* pskBase64, size_t pskBase64Size, const char* channelName) {
    if (!isHashtagChannelName(channelName) || pskBase64Size < 25) {
        return false;
    }

    uint8_t secret[16];
    mesh::Utils::sha256(secret, sizeof(secret), (const uint8_t*) channelName, strlen(channelName));
    return encodeBase64Psk16(pskBase64, pskBase64Size, secret);
}

inline ChannelDetails* addHashtagChannel(MyMesh* mesh, GUI* gui, Menu* menu, const char* channelName) {
    char pskBase64[25];
    if (!generateHashtagChannelPskBase64(pskBase64, sizeof(pskBase64), channelName)) {
        return nullptr;
    }

    return addSecretChannel(mesh, gui, menu, channelName, pskBase64);
}

#if defined(__has_include)
    #if __has_include("secret.local.h")
        #include "secret.local.h"
    #else
        inline void addSecrets(MyMesh* mesh, GUI* gui, Menu* menu) {
            (void) mesh;
            (void) gui;
            (void) menu;
        }
    #endif
#else
    inline void addSecrets(MyMesh* mesh, GUI* gui, Menu* menu) {
        (void) mesh;
        (void) gui;
        (void) menu;
    }
#endif
