#pragma once

// Copy this file to secret.local.h and replace the placeholder PSKs.
// secret.local.h is git-ignored, so you can keep private channels there.
// Hashtag channels must match #[a-z0-9]+ and use a deterministic 16-byte key.

#define PUBLIC_GROUP_PSK "izOH6cXN6mrJ5e26oRXNcg=="

inline void addSecrets(MyMesh* mesh, GUI* gui, Menu* menu) {
    addSecretChannel(mesh, gui, menu, "Public", PUBLIC_GROUP_PSK);
    addSecretChannel(mesh, gui, menu, "My Channel", "replace-with-base64-psk");
    addHashtagChannel(mesh, gui, menu, "#testing");
}
