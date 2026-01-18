#pragma once

#include "menu.h"
#include "MyMesh.h"

#define TESTING_GROUP_PSK               "zeXoLPUVZH3LVHp5pPBl0Q=="

void addSecrets(MyMesh* mesh, GUI* gui, Menu* menu) {
    ChannelDetails* _testing = mesh->addChannel("#testing", TESTING_GROUP_PSK);
    mesh->channels.push_back(_testing);

    Channel* tes = new Channel(gui, _testing);

    MIPage* mites = new MIPage(gui, "#testing", tes);

    menu->add(mites);
}
