#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <helpers/BaseChatMesh.h>
#include <InternalFileSystem.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <vector>
#include <RTClib.h>

#include "NodePrefs.h"
#include "MeshTables.h"
#include "menu.h"

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

class MyMesh : public BaseChatMesh, ContactVisitor {
  FILESYSTEM* _fs;
  uint32_t expected_ack_crc;
  unsigned long last_msg_sent;
  ContactInfo* curr_recipient;
  char command[512+10];
  uint8_t tmp_buf[256];
  char hex_buf[512];
  GUI* gui;

  bool advClockSync = false;
  
  const char* getTypeName(uint8_t type) const {
    if (type == ADV_TYPE_CHAT) return "Chat";
    if (type == ADV_TYPE_REPEATER) return "Repeater";
    if (type == ADV_TYPE_ROOM) return "Room";
    return "??";  // unknown
  }

  void loadContacts();
  void saveContacts();
  void setClock(uint32_t timestamp, bool force);
protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  int calcRxDelay(float score, uint32_t air_time) const override {
    //return 0;  // disable rxdelay TODO: could this save some power??
    return (int)((pow(10.0f, 0.85f - score) - 1.0) * air_time);
  }

  bool allowPacketForward(const mesh::Packet* packet) override { return false; }
  void onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len);
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo& contact) override;
  ContactInfo* processAck(const uint8_t *data) override;
  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override;
  void onCommandDataRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {}
  void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) override {}
  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) override;
  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override {}

  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override {
    return 0;  // unknown
  }

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
    return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
  }
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override {
    return SEND_TIMEOUT_BASE_MILLIS + 
         ( (pkt_airtime_millis*DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) * (path_len + 1));
  }

  void onSendTimeout() override {};

public:
  const int32_t gmtOffset = 3600 * 2; // TODO: make configurable

  NodePrefs _prefs;
  ChannelDetails* _public;

  std::vector<message>* messages;
  std::vector<message>* outmessages;

  std::vector<ChannelDetails*> channels;

  MyMesh(mesh::Radio& radio, StdRNG& rng, mesh::RTCClock& rtc, LcdMeshTables& tables, GUI* gui, std::vector<message>* messages, std::vector<message>* outmessages);

  float getFreqPref() const { return _prefs.freq; }
  uint8_t getTxPowerPref() const { return _prefs.tx_power_dbm; }

  void begin(FILESYSTEM& fs);
  void savePrefs();

  void sendSelfAdvert(int delay_millis, bool flood);
  void onContactVisit(const ContactInfo& contact) override;

  void loop() { BaseChatMesh::loop(); }

  LcdMeshTables* getMeshTables() {
    return (LcdMeshTables*) getTables();
  }
};
