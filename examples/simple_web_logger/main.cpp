#include <Arduino.h> // needed for PlatformIO
#include <Mesh.h>

// export PATH="/root/.platformio/penv/bin:$PATH"

#ifndef ESP32
  #error "Platform not supported."
#endif

#include <SPIFFS.h>
#include <WiFi.h>
#include "time.h"
#include <queue>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/IdentityStore.h>
#include <RTClib.h>
#include <target.h>
#include "LoggerMeshTables.h"

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#define FIRMWARE_VER_TEXT   "v2 (build: 4 Feb 2025)"

#ifndef LORA_FREQ
  #define LORA_FREQ   915.0
#endif
#ifndef LORA_BW
  #define LORA_BW     250
#endif
#ifndef LORA_SF
  #define LORA_SF     10
#endif
#ifndef LORA_CR
  #define LORA_CR      5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  20
#endif

#ifndef MAX_CONTACTS
  #define MAX_CONTACTS         100
#endif

#include <helpers/BaseChatMesh.h>

#define SEND_TIMEOUT_BASE_MILLIS          500
#define FLOOD_SEND_TIMEOUT_FACTOR         16.0f
#define DIRECT_SEND_PERHOP_FACTOR         6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS   250

#define  PUBLIC_GROUP_PSK "izOH6cXN6mrJ5e26oRXNcg=="

static bool ntpSynced = false;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 60 * 60 * 3;
const int   daylightOffset_sec = 3600;

// RTOS, wifi thread
TaskHandle_t WiFiTask;
void WiFiTaskCode(void* pvParameters);

void task_sleep(uint32_t ms) {
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

struct {
  std::queue<char*> queue;
  unsigned discarded;

  void push(String str) {
    unsigned bef = ESP.getFreeHeap();

    char* msgData = new char[str.length() + 1];
    str.toCharArray(msgData, str.length() + 1);
  
    // discard old
    while (queue.size() > 32) {
      discarded++;
      char* ptr = queue.front();
      Serial.printf("Discarded message (%u) %s\n", discarded, ptr);
      queue.pop();
      delete[] ptr;
    }
    queue.push(msgData);

    unsigned aft = ESP.getFreeHeap();
  }

  void push(const JsonDocument& doc) {
    String postData;
    serializeJson(doc, postData);
    push(postData);
  }

  size_t size() { return queue.size(); }
  char* front() { return queue.front(); }
  void  pop()   { queue.pop(); }
} messageQueue;

unsigned long getTimestamp() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return(0);
  }
  time(&now);
  return now;
}

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

/* -------------------------------------------------------------------------------------- */

struct NodePrefs {  // persisted to file
  float airtime_factor;
  char node_name[32];
  double node_lat, node_lon;
  float freq;
  uint8_t tx_power_dbm;
  uint8_t unused[3];
};

struct WiFiPrefs {
  char ssid[33];
  char password[64];
};

struct LogPrefs {
  uint16_t version;
  uint32_t selfreport;
  char auth[32];
  char url[256];
  uint8_t doraw;
  uint8_t dofwd;
};

class MyMesh : public BaseChatMesh, ContactVisitor {
  FILESYSTEM* _fs;
  LoggerMeshTables* _tables;
  NodePrefs _prefs;
  WiFiPrefs _wifi;
  LogPrefs _logp;
  uint32_t expected_ack_crc;
  ChannelDetails* _public;
  unsigned long last_msg_sent;
  ContactInfo* curr_recipient;
  char command[512+10];
  uint8_t tmp_buf[256];
  char hex_buf[512];

  // debug toggle flag
  bool m_debugPrint = false;

  const char* getTypeName(uint8_t type) const {
    if (type == ADV_TYPE_CHAT) return "Chat";
    if (type == ADV_TYPE_REPEATER) return "Repeater";
    if (type == ADV_TYPE_ROOM) return "Room";
    return "??";  // unknown
  }

  void loadContacts() {
    if (_fs->exists("/contacts")) {
      File file = _fs->open("/contacts");
      if (file) {
        bool full = false;
        while (!full) {
          ContactInfo c;
          uint8_t pub_key[32];
          uint8_t unused;
          uint32_t reserved;

          bool success = (file.read(pub_key, 32) == 32);
          success = success && (file.read((uint8_t *) &c.name, 32) == 32);
          success = success && (file.read(&c.type, 1) == 1);
          success = success && (file.read(&c.flags, 1) == 1);
          success = success && (file.read(&unused, 1) == 1);
          success = success && (file.read((uint8_t *) &reserved, 4) == 4);
          success = success && (file.read((uint8_t *) &c.out_path_len, 1) == 1);
          success = success && (file.read((uint8_t *) &c.last_advert_timestamp, 4) == 4);
          success = success && (file.read(c.out_path, 64) == 64);
          c.gps_lat = c.gps_lon = 0;   // not yet supported

          if (!success) break;  // EOF

          c.id = mesh::Identity(pub_key);
          c.lastmod = 0;
          if (!addContact(c)) full = true;
        }
        file.close();
      }
    }
  }

  void saveContacts() {
    File file = _fs->open("/contacts", "w", true);
    if (file) {
      ContactsIterator iter;
      ContactInfo c;
      uint8_t unused = 0;
      uint32_t reserved = 0;

      while (iter.hasNext(this, c)) {
        bool success = (file.write(c.id.pub_key, 32) == 32);
        success = success && (file.write((uint8_t *) &c.name, 32) == 32);
        success = success && (file.write(&c.type, 1) == 1);
        success = success && (file.write(&c.flags, 1) == 1);
        success = success && (file.write(&unused, 1) == 1);
        success = success && (file.write((uint8_t *) &reserved, 4) == 4);
        success = success && (file.write((uint8_t *) &c.out_path_len, 1) == 1);
        success = success && (file.write((uint8_t *) &c.last_advert_timestamp, 4) == 4);
        success = success && (file.write(c.out_path, 64) == 64);

        if (!success) break;  // write failed
      }
      file.close();
    }
  }

public:
  void setClock(uint32_t timestamp, bool ntp) {
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (timestamp > curr || ntp) {
      getRTCClock()->setCurrentTime(timestamp);
      if (!ntp && !ntpSynced) {
        Serial.println("   Synced local");
        timeval epoch = {timestamp, 0};
        settimeofday((const timeval*)&epoch, 0);
        // update local
      } else {
        ntpSynced = true;
      }
      Serial.println("   (OK - clock set!)");
    } else {
      Serial.println("   (ERR: clock cannot go backwards)");
    }
  }

  const NodePrefs* getNodePrefs() { return &_prefs; }
  const LogPrefs* getLogPrefs() { return &_logp; }
  const bool debugPrint() { return m_debugPrint; }

private:
  void importCard(const char* command) {
    while (*command == ' ') command++;   // skip leading spaces
    if (memcmp(command, "meshcore://", 11) == 0) {
      command += 11;  // skip the prefix
      char *ep = strchr(command, 0);  // find end of string
      while (ep > command) {
        ep--;
        if (mesh::Utils::isHexChar(*ep)) break;  // found tail end of card
        *ep = 0;  // remove trailing spaces and other junk
      }
      int len = strlen(command);
      if (len % 2 == 0) {
        len >>= 1;  // halve, for num bytes
        if (mesh::Utils::fromHex(tmp_buf, len, command)) {
          importContact(tmp_buf, len);
          return;
        }
      }
    }
    Serial.println("   error: invalid format");
  }

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  int calcRxDelay(float score, uint32_t air_time) const override {
    return 0;  // disable rxdelay
  }

  bool allowPacketForward(const mesh::Packet* packet) override {
    return _logp.dofwd && !_tables->hasSeen2(packet);
  }

  //void BaseChatMesh::onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len)

  const char* type2str(int type) {
    if(type == PAYLOAD_TYPE_REQ) return "REQ";
    else if(type == PAYLOAD_TYPE_RESPONSE) return "RESPONSE";
    else if(type == PAYLOAD_TYPE_TXT_MSG) return "TXT_MSG";
    else if(type == PAYLOAD_TYPE_ACK) return "ACK";
    else if(type == PAYLOAD_TYPE_ADVERT) return "ADVERT";
    else if(type == PAYLOAD_TYPE_GRP_TXT) return "GRP_TXT";
    else if(type == PAYLOAD_TYPE_GRP_DATA) return "GRP_DATA";
    else if(type == PAYLOAD_TYPE_ANON_REQ) return "ANON_REQ";
    else if(type == PAYLOAD_TYPE_PATH) return "PATH";
    else if(type == PAYLOAD_TYPE_TRACE) return "TRACE";
    return "Unknown";
  } 

  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override {
    // send raw
    if (_logp.doraw) {
      int phType = (pkt->header >> PH_TYPE_SHIFT) & PH_TYPE_MASK;

      if (debugPrint()) {
        Serial.println("[RAW] Received packet:");
        Serial.printf("      header:          %u\n", pkt->header);
        Serial.printf("        route-type:    %u\n", pkt->header & PH_ROUTE_MASK); // 2 bits
        Serial.printf("        payload-type:  %s (%u)\n", type2str(phType), phType); // 4 bits
        Serial.printf("        payload-vers:  %u\n", (pkt->header >> PH_VER_SHIFT) & PH_VER_MASK); // 2 bits
        Serial.printf("      payload_len:     %u\n", pkt->payload_len);
        Serial.printf("      path_len:        %u\n", pkt->path_len);
        Serial.printf("      transport_codes: %u %u\n", pkt->transport_codes[0], pkt->transport_codes[1]);
        Serial.printf("      snr:             %i\n", pkt->_snr);
        Serial.println();
      }

      char sender[(PUB_KEY_SIZE * 2) + 1];
      char path[(pkt->path_len * 2) + 1];
      char payload[(pkt->payload_len * 2) + 1];
      mesh::Utils::toHex(sender, self_id.pub_key, PUB_KEY_SIZE);
      mesh::Utils::toHex(path, pkt->path, pkt->path_len);
      mesh::Utils::toHex(payload, pkt->payload, pkt->payload_len);

      char hashstr[(MAX_HASH_SIZE * 2) + 1];
      uint8_t hash[MAX_HASH_SIZE];
      pkt->calculatePacketHash(hash);
      mesh::Utils::toHex(hashstr, hash, MAX_HASH_SIZE);

      JsonDocument doc;
      doc["version"] = 1;
      doc["type"] = "RAW";
      doc["reporter"] = sender;
      doc["hash"] = hashstr;
      doc["recvtime"] = getRTCClock()->getCurrentTime();
      doc["packet"]["header"]["raw"] = pkt->header;
      doc["packet"]["header"]["route-type"] = pkt->header & PH_ROUTE_MASK;
      doc["packet"]["header"]["payload-type"] = (pkt->header >> PH_TYPE_SHIFT) & PH_TYPE_MASK;
      doc["packet"]["header"]["payload-version"] = (pkt->header >> PH_VER_SHIFT) & PH_VER_MASK;
      doc["packet"]["path"] = path;
      doc["packet"]["payload"] = payload;
      doc["packet"]["snr"] = pkt->_snr;
      messageQueue.push(doc);
    }

    return Mesh::onRecvPacket(pkt);
  }

  void onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) {
    ContactInfo* from = lookupContactByPubKey(id.pub_key, PUB_KEY_SIZE);
    bool is_new = from == NULL;
    BaseChatMesh::onAdvertRecv(pkt, id, timestamp, app_data, app_data_len);  // chain to super impl
    from = lookupContactByPubKey(id.pub_key, PUB_KEY_SIZE);

    if (!from) {
      Serial.println("ERROR: onAdvertRecv: Contact not found!");
      return;
    }

    AdvertDataParser parser(app_data, app_data_len);
    if (!(parser.isValid() && parser.hasName())) {
      Serial.printf("ERROR: onAdvertRecv: invalid app_data, or name is missing: len=%d\n", app_data_len);
      return;
    }

    char pubkey[(PUB_KEY_SIZE * 2) + 1];
    char sender[(PUB_KEY_SIZE * 2) + 1];

    mesh::Utils::toHex(pubkey, id.pub_key, PUB_KEY_SIZE);
    mesh::Utils::toHex(sender, self_id.pub_key, PUB_KEY_SIZE);

    char hashstr[(MAX_HASH_SIZE * 2) + 1];
    uint8_t hash[MAX_HASH_SIZE];
    pkt->calculatePacketHash(hash);
    mesh::Utils::toHex(hashstr, hash, MAX_HASH_SIZE);

    JsonDocument doc;
    doc["version"] = 1;
    doc["type"] = "ADV";
    doc["reporter"] = sender;
    doc["hash"] = hashstr;
    doc["snr"] = pkt->getSNR();
    doc["time"]["local"] = getRTCClock()->getCurrentTime();
    doc["time"]["sender"] = timestamp;
    doc["contact"]["new"] = is_new;
    doc["contact"]["type"] = from->type;
    doc["contact"]["flags"] = from->flags;
    doc["contact"]["name"] = from->name;
    doc["contact"]["pubkey"] = pubkey;
    doc["contact"]["lat"] = parser.getIntLat();
    doc["contact"]["lon"] = parser.getIntLon();
    doc["message"]["path"] = getPath(pkt);
    messageQueue.push(doc);

    // Serial prints
    if (debugPrint()) {
      Serial.printf("ADVERT from -> %s\n", from->name);
      Serial.printf("  lat:       %.6f\n", parser.getIntLat() / 1000000.0);
      Serial.printf("  lon:       %.6f\n", parser.getIntLon() / 1000000.0);
    }
  }

  void onDiscoveredContact(ContactInfo& contact, bool is_new) override {
    saveContacts();
  }

  String getPath(mesh::Packet* pkt) {
    String path = "";
    if (!pkt->isRouteDirect()) {
      char buf[4];
      for (size_t i = 0; i < pkt->path_len; i++) {
        sprintf(buf, "%02x", pkt->path[i]);
        if (i != 0) path += ",";
        path += buf;
      }
    }
    return path;
  }

  void onContactPathUpdated(const ContactInfo& contact) override {
    Serial.printf("PATH to: %s, path_len=%d\n", contact.name, (int32_t) contact.out_path_len);
    saveContacts();
  }

  bool processAck(const uint8_t *data) override {
    if (memcmp(data, &expected_ack_crc, 4) == 0) {     // got an ACK from recipient
      Serial.printf("   Got ACK! (round trip: %d millis)\n", _ms->getMillis() - last_msg_sent);
      // NOTE: the same ACK can be received multiple times!
      expected_ack_crc = 0;  // reset our expected hash, now that we have received ACK
      return true;
    }

    //uint32_t crc;
    //memcpy(&crc, data, 4);
    //MESH_DEBUG_PRINTLN("unknown ACK received: %08X (expected: %08X)", crc, expected_ack_crc);
    return false;
  }

  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
    char pubkey[(PUB_KEY_SIZE * 2) + 1];
    char sender[(PUB_KEY_SIZE * 2) + 1];

    mesh::Utils::toHex(pubkey, from.id.pub_key, PUB_KEY_SIZE);
    mesh::Utils::toHex(sender, self_id.pub_key, PUB_KEY_SIZE);

    char hashstr[(MAX_HASH_SIZE * 2) + 1];
    uint8_t hash[MAX_HASH_SIZE];
    pkt->calculatePacketHash(hash);
    mesh::Utils::toHex(hashstr, hash, MAX_HASH_SIZE);

    JsonDocument doc;
    doc["version"] = 1;
    doc["type"] = "MSG";
    doc["reporter"] = sender;
    doc["hash"] = hashstr;
    doc["snr"] = pkt->getSNR();
    doc["time"]["local"] = getRTCClock()->getCurrentTime();
    doc["time"]["sender"] = sender_timestamp;
    doc["contact"]["type"] = from.type;
    doc["contact"]["flags"] = from.flags;
    doc["contact"]["pubkey"] = pubkey;
    doc["contact"]["name"] = from.name;
    doc["contact"]["lat"] = from.gps_lat;
    doc["contact"]["lon"] = from.gps_lon;
    doc["message"]["text"] = text;
    doc["message"]["header"] = pkt->header;
    doc["message"]["path"] = getPath(pkt);
    messageQueue.push(doc);

    // Serial prints
    Serial.printf("MESSAGE from -> %s\n", from.name);

    // Special commands
    if (strcmp(text, "clock sync") == 0) {  // special text command
      setClock(sender_timestamp + 1, false);
    } else if (memcmp(text, "echo ", 5) == 0) {  // special text command
      const char* echo = &text[5];
      uint32_t est_timeout;
      last_msg_sent = _ms->getMillis();
      sendMessage(from, getRTCClock()->getCurrentTime(), 0, echo, expected_ack_crc, est_timeout);
    }
  }

  void onCommandDataRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
    // logmsg* m = new logmsg;
    // m->prio = "4";
    // m->time = sender_timestamp;
    // m->tag = from.name;
    // m->msg = "Command: ";
    // m->msg += text;
    // pendingMsgs.push_back(m);
  }
  
  void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) override {
  }

  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) override {
    char chhash[(PUB_KEY_SIZE * 2) + 1];
    char sender[(PUB_KEY_SIZE * 2) + 1];

    mesh::Utils::toHex(chhash, channel.hash, PATH_HASH_SIZE);
    mesh::Utils::toHex(sender, self_id.pub_key, PUB_KEY_SIZE);

    char hashstr[(MAX_HASH_SIZE * 2) + 1];
    uint8_t hash[MAX_HASH_SIZE];
    pkt->calculatePacketHash(hash);
    mesh::Utils::toHex(hashstr, hash, MAX_HASH_SIZE);

    JsonDocument doc;
    doc["version"] = 1;
    doc["type"] = "PUB";
    doc["reporter"] = sender;
    doc["hash"] = hashstr;
    doc["snr"] = pkt->getSNR();
    doc["time"]["local"] = getRTCClock()->getCurrentTime();
    doc["time"]["sender"] = timestamp;
    doc["message"]["text"] = text;
    doc["message"]["header"] = pkt->header;
    doc["message"]["path"] = getPath(pkt);
    messageQueue.push(doc);

    if (pkt->isRouteDirect()) {
      Serial.printf("PUBLIC CHANNEL MSG -> (Direct!)\n");
    } else {
      Serial.printf("PUBLIC CHANNEL MSG -> (Flood) hops %d\n", pkt->path_len);
    }

    Serial.printf("   %s\n", text);
  }
  
  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override {
    return 0;  // unknown
  }

  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override {
    // not supported
  }

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
    return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
  }
  
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override {
    return SEND_TIMEOUT_BASE_MILLIS + 
         ( (pkt_airtime_millis*DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) * (path_len + 1));
  }

  void onSendTimeout() override {
    Serial.println("   ERROR: timed out, no ACK.");
  }

public:
  MyMesh(mesh::Radio& radio, StdRNG& rng, mesh::RTCClock& rtc, LoggerMeshTables& tables)
     : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables)
  {
    // defaults
    memset(&_prefs, 0, sizeof(_prefs));
    memset(&_wifi, 0, sizeof(_wifi));
    memset(&_logp, 0, sizeof(_logp));
    _prefs.airtime_factor = 2.0;    // one third
    strcpy(_prefs.node_name, "NONAME");
    _prefs.freq = LORA_FREQ;
    _prefs.tx_power_dbm = LORA_TX_POWER;
    _tables = &tables;

    command[0] = 0;
    curr_recipient = NULL;
  }

  float getFreqPref() const { return _prefs.freq; }
  uint8_t getTxPowerPref() const { return _prefs.tx_power_dbm; }

  const uint8_t* getPubKey() {
    return self_id.pub_key;
  }

  void begin(FILESYSTEM& fs) {
    _fs = &fs;

    BaseChatMesh::begin();

    IdentityStore store(fs, "/identity");

    if (!store.load("_main", self_id, _prefs.node_name, sizeof(_prefs.node_name))) {  // legacy: node_name was from identity file
      // Need way to get some entropy to seed RNG
      Serial.println("Press ENTER to generate key:");
      char c = 0;
      while (c != '\n') {   // wait for ENTER to be pressed
        if (Serial.available()) c = Serial.read();
      }
      Serial.println("generating key...");
      ((StdRNG *)getRNG())->begin(millis());

      self_id = mesh::LocalIdentity(getRNG());  // create new random identity
      int count = 0;
      while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
        self_id = mesh::LocalIdentity(getRNG()); count++;
      }
      Serial.println("done.");
      store.save("_main", self_id);
    }

    // load persisted prefs
    if (_fs->exists("/node_prefs")) {
      File file = _fs->open("/node_prefs");
      if (file) {
        file.read((uint8_t *) &_prefs, sizeof(_prefs));
        file.close();
      }
    }

    // load wifi prefs
    if (_fs->exists("/wifi_prefs")) {
      File file = _fs->open("/wifi_prefs");
      if (file) {
        file.read((uint8_t *) &_wifi, sizeof(_wifi));
        file.close();
      }
    }

    // load wifi prefs
    if (_fs->exists("/log_prefs")) {
      File file = _fs->open("/log_prefs");
      if (file) {
        file.read((uint8_t *) &_logp, sizeof(_logp));
        file.close();

        if (_logp.version == 0) {
          _logp.version = 1;
          _logp.selfreport = 15 * 60; // 15 min default
          saveLogPrefs();
        }
      }
    }

    loadContacts();
    _public = addChannel("Test", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel

    toggleWiFi(true);
  }

  void toggleWiFi(bool enable) {
    if (strlen(_wifi.ssid) < 1) {
      Serial.println("WiFi: SSID not set");
      return;
    }
    if (enable) {
      Serial.printf("WiFi: Conencting to %s\n", _wifi.ssid);
      WiFi.mode(WIFI_STA);
      WiFi.begin(_wifi.ssid, _wifi.password);
    } else {
      Serial.println("WiFi: Disconencting");
      WiFi.disconnect();
    }
  }

  void savePrefs() {
    File file = _fs->open("/node_prefs", "w", true);
    if (file) {
      file.write((const uint8_t *)&_prefs, sizeof(_prefs));
      file.close();
    }
  }

  void saveWiFiPrefs() {
    File file = _fs->open("/wifi_prefs", "w", true);
    if (file) {
      file.write((const uint8_t *)&_wifi, sizeof(_wifi));
      file.close();
    }

    toggleWiFi(false);
    toggleWiFi(true);
  }

  void saveLogPrefs() {
    File file = _fs->open("/log_prefs", "w", true);
    if (file) {
      file.write((const uint8_t *)&_logp, sizeof(_logp));
      file.close();
    }
  }

  void showWelcome() {
    Serial.println("===== MeshCore Chat Terminal =====");
    Serial.println();
    Serial.printf("WELCOME  %s\n", _prefs.node_name);
    mesh::Utils::printHex(Serial, self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();
    Serial.println("   (enter 'help' for basic commands)");
    Serial.println();
  }

  void sendSelfAdvert(int delay_millis) {
    auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
    if (pkt) {
      sendFlood(pkt, delay_millis);
    }
  }

  // ContactVisitor
  void onContactVisit(const ContactInfo& contact) override {
    Serial.printf("   %s - ", contact.name);
    char tmp[40];
    int32_t secs = contact.last_advert_timestamp - getRTCClock()->getCurrentTime();
    AdvertTimeHelper::formatRelativeTimeDiff(tmp, secs, false);
    Serial.println(tmp);
  }

  void handleCommand(const char* command) {
    while (*command == ' ') command++;  // skip leading spaces

    if (memcmp(command, "send ", 5) == 0) {
      if (curr_recipient) {
        const char *text = &command[5];
        uint32_t est_timeout;

        int result = sendMessage(*curr_recipient, getRTCClock()->getCurrentTime(), 0, text, expected_ack_crc, est_timeout);
        if (result == MSG_SEND_FAILED) {
          Serial.println("   ERROR: unable to send.");
        } else {
          last_msg_sent = _ms->getMillis();
          Serial.printf("   (message sent - %s)\n", result == MSG_SEND_SENT_FLOOD ? "FLOOD" : "DIRECT");
        }
      } else {
        Serial.println("   ERROR: no recipient selected (use 'to' cmd).");
      }
    } else if (memcmp(command, "public ", 7) == 0) {  // send GroupChannel msg
      uint8_t temp[5+MAX_TEXT_LEN+32];
      uint32_t timestamp = getRTCClock()->getCurrentTime();
      memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
      temp[4] = 0;  // attempt and flags

      sprintf((char *) &temp[5], "%s: %s", _prefs.node_name, &command[7]);  // <sender>: <msg>
      temp[5 + MAX_TEXT_LEN] = 0;  // truncate if too long

      int len = strlen((char *) &temp[5]);
      auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, _public->channel, temp, 5 + len);
      if (pkt) {
        sendFlood(pkt);
        Serial.println("   Sent.");
      } else {
        Serial.println("   ERROR: unable to send");
      }
    } else if (memcmp(command, "list", 4) == 0) {  // show Contact list, by most recent
      int n = 0;
      if (command[4] == ' ') {  // optional param, last 'N'
        n = atoi(&command[5]);
      }
      scanRecentContacts(n, this);
    } else if (strcmp(command, "clock") == 0) {    // show current time
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      Serial.printf(   "%02d:%02d - %d/%d/%d UTC\n", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else if (memcmp(command, "time ", 5) == 0) {  // set time (to epoch seconds)
      const char* config = &command[5];
      if (memcmp(config, "ntp", 3) == 0) {
        ntpSynced = false;
      } else {
        uint32_t secs = _atoi(config);
        setClock(secs, false);
      }
    } else if (memcmp(command, "to ", 3) == 0) {  // set current recipient
      curr_recipient = searchContactsByPrefix(&command[3]);
      if (curr_recipient) {
        Serial.printf("   Recipient %s now selected.\n", curr_recipient->name);
      } else {
        Serial.println("   Error: Name prefix not found.");
      }
    } else if (strcmp(command, "to") == 0) {    // show current recipient
      if (curr_recipient) {
         Serial.printf("   Current: %s\n", curr_recipient->name);
      } else {
         Serial.println("   Err: no recipient selected");
      }
    } else if (strcmp(command, "advert") == 0) {
      auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
      if (pkt) {
        sendZeroHop(pkt);
        Serial.println("   (advert sent, zero hop).");
      } else {
        Serial.println("   ERR: unable to send");
      }
    } else if (strcmp(command, "flood") == 0) {
      auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
      if (pkt) {
        sendFlood(pkt);
        Serial.println("   (advert sent, flood).");
      } else {
        Serial.println("   ERR: unable to send");
      }
    } else if (strcmp(command, "reset path") == 0) {
      if (curr_recipient) {
        resetPathTo(*curr_recipient);
        saveContacts();
        Serial.println("   Done.");
      }
    } else if (memcmp(command, "card", 4) == 0) {
      Serial.printf("Hello %s\n", _prefs.node_name);
      auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
      if (pkt) {
        uint8_t len =  pkt->writeTo(tmp_buf);
        releasePacket(pkt);  // undo the obtainNewPacket()

        mesh::Utils::toHex(hex_buf, tmp_buf, len);
        Serial.println("Your MeshCore biz card:");
        Serial.print("meshcore://"); Serial.println(hex_buf);
        Serial.println();
      } else {
        Serial.println("  Error");
      }
    } else if (memcmp(command, "import ", 7) == 0) {
      importCard(&command[7]);
    } else if (memcmp(command, "set ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af ", 3) == 0) {
        _prefs.airtime_factor = atof(&config[3]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "name ", 5) == 0) {
        StrHelper::strncpy(_prefs.node_name, &config[5], sizeof(_prefs.node_name));
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "lat ", 4) == 0) {
        _prefs.node_lat = atof(&config[4]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "lon ", 4) == 0) {
        _prefs.node_lon = atof(&config[4]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "tx ", 3) == 0) {
        _prefs.tx_power_dbm = atoi(&config[3]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else if (memcmp(config, "freq ", 5) == 0) {
        _prefs.freq = atof(&config[5]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else {
        Serial.printf("  ERROR: unknown config: %s\n", config);
      }
    } else if (memcmp(command, "ver", 3) == 0) {
      Serial.println(FIRMWARE_VER_TEXT);
    } else if (memcmp(command, "wifi ", 5) == 0) {
      const char* config = &command[5];
      if (memcmp(config, "ssid ", 5) == 0) {
        StrHelper::strncpy(_wifi.ssid, &config[5], sizeof(_wifi.ssid));
        saveWiFiPrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "password ", 9) == 0) {
        StrHelper::strncpy(_wifi.password, &config[9], sizeof(_wifi.password));
        saveWiFiPrefs();
        Serial.println("  OK");
      } else {
        Serial.printf("  WiFi Conencted: %u\n", WiFi.status() == WL_CONNECTED);
      }
    } else if (memcmp(command, "log ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "url ", 4) == 0) {
        StrHelper::strncpy(_logp.url, &config[4], sizeof(_logp.url));
        saveLogPrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "auth ", 5) == 0) {
        StrHelper::strncpy(_logp.auth, &config[5], sizeof(_logp.auth));
        saveLogPrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "report ", 7) == 0) {
        _logp.selfreport = atoi(&config[7]);
        saveLogPrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "raw ", 4) == 0) {
        if (config[4] == 'y') {
          _logp.doraw = 1;
        } else {
          _logp.doraw = 0;
        }
        saveLogPrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "fwd ", 4) == 0) {
        if (config[4] == 'y') {
          _logp.dofwd = 1;
        } else {
          _logp.dofwd = 0;
        }
        saveLogPrefs();
        Serial.println("  OK");
      } else {
        char sender[(PUB_KEY_SIZE * 2) + 1];
        mesh::Utils::toHex(sender, self_id.pub_key, PUB_KEY_SIZE);
        Serial.printf("  Log url:     %s\n", _logp.url);
        Serial.printf("  Self-report: %u\n", _logp.selfreport);
        Serial.printf("  Pub Key:     %s\n", sender);
        Serial.printf("  Raw:         %u\n", _logp.doraw);
      }
    } else if (memcmp(command, "debug ", 6) == 0) { 
      if (command[6] == 'y') {
        m_debugPrint = true;
      } else {
        m_debugPrint = false;
      }
      Serial.printf("  Debug print: %u\n", m_debugPrint);
    } else if (memcmp(command, "regeneratekey", 13) == 0) {
      IdentityStore store(*_fs, "/identity");
      Serial.println("generating key...");
      ((StdRNG *)getRNG())->begin(millis());
    
      self_id = mesh::LocalIdentity(getRNG());  // create new random identity
      int count = 0;
      while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
        self_id = mesh::LocalIdentity(getRNG()); count++;
      }
      Serial.println("done.");
      store.save("_main", self_id);
    } else if (memcmp(command, "help", 4) == 0) {
      Serial.println("Commands:");
      Serial.println("   set {name|lat|lon|freq|tx|af} {value}");
      Serial.println("   card");
      Serial.println("   import {biz card}");
      Serial.println("   clock");
      Serial.println("   time {epoch-seconds|ntp}>");
      Serial.println("   list {n}");
      Serial.println("   to <recipient name or prefix>");
      Serial.println("   to");
      Serial.println("   send <text>");
      Serial.println("   advert");
      Serial.println("   flood");
      Serial.println("   reset path");
      Serial.println("   public <text>");
      Serial.println("   wifi {ssid|password} {value}");
      Serial.println("   log {url|auth|report|raw} {value}");
    } else {
      Serial.print("   ERROR: unknown command: "); Serial.println(command);
    }
  }

  void loop() {
    BaseChatMesh::loop();

    int len = strlen(command);
    while (Serial.available() && len < sizeof(command)-1) {
      char c = Serial.read();
      if (c != '\n') { 
        command[len++] = c;
        command[len] = 0;
      }
      Serial.print(c);
    }
    if (len == sizeof(command)-1) {  // command buffer full
      command[sizeof(command)-1] = '\r';
    }

    if (len > 0 && command[len - 1] == '\r') {  // received complete line
      command[len - 1] = 0;  // replace newline with C string null terminator

      handleCommand(command);
      command[0] = 0;  // reset command buffer
    }
  }
};

StdRNG fast_rng;
LoggerMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, *new VolatileRTCClock(), tables); // TODO: test with 'rtc_clock' in target.cpp

void halt() {
  while (1) ;
}

void WiFiTaskCode(void * pvParameters) {
  static bool connected = false;
  static bool sendsys   = false;
  static unsigned sendFailures = 0;
  static unsigned long lastConencted = 0;
  static unsigned long nextReport = 30000;

  Serial.print("Task1 running on core ");
  Serial.println(xPortGetCoreID());

  WiFiClientSecure* client = new WiFiClientSecure;
  client->setInsecure();

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      sendsys = false;
      lastConencted = millis();
      if (!ntpSynced) {
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        unsigned time = getTimestamp();
        the_mesh.setClock(time, true);
      }

      if (the_mesh.getLogPrefs()->selfreport > 0 && millis() > nextReport) {
        char sender[(PUB_KEY_SIZE * 2) + 1];
        mesh::Utils::toHex(sender, the_mesh.getPubKey(), PUB_KEY_SIZE);
  
        JsonDocument doc;
        doc["version"] = 1;
        doc["type"] = "SYS";
        doc["reporter"] = sender;
        doc["time"]["local"] = getTimestamp(); //()->getCurrentTime();
        doc["sys"]["type"] = "status";
        doc["sys"]["heap_total"] = ESP.getHeapSize();
        doc["sys"]["heap_free"] = ESP.getFreeHeap();
        doc["sys"]["rssi"] = WiFi.RSSI();
        doc["sys"]["uptime"] = millis();
        doc["contact"]["new"] = false;
        doc["contact"]["type"] = ADV_TYPE_CHAT;
        doc["contact"]["flags"] = 0;
        doc["contact"]["name"] = the_mesh.getNodePrefs()->node_name;
        doc["contact"]["pubkey"] = sender;
        doc["contact"]["lat"] = the_mesh.getNodePrefs()->node_lat;
        doc["contact"]["lon"] = the_mesh.getNodePrefs()->node_lon;
        messageQueue.push(doc);
        if (the_mesh.getLogPrefs()->selfreport != -1) {
          nextReport = millis() + (the_mesh.getLogPrefs()->selfreport * 1000);
        }
      }

      if (messageQueue.size() > 0) {
        String auth = "Bearer ";
        auth += the_mesh.getLogPrefs()->auth;

        if (memcmp(the_mesh.getLogPrefs()->url, "http", 4) != 0) {
          Serial.println("URL Not set");
          continue;
        }

        char *ptr = messageQueue.front();
        if (ptr != nullptr) {
          if (the_mesh.debugPrint()) {
            Serial.printf("Queue peek %s\n", ptr);
          }
          bool sent = false;

          // WiFi send
          HTTPClient https;

          if (https.begin(*client, the_mesh.getLogPrefs()->url)) {  // HTTPS connection
            https.addHeader("Content-Type", "application/json");

            if (auth.length() > 7) {
              https.addHeader("Authorization", auth);
            }

            if (the_mesh.debugPrint()) {
              Serial.println("[HTTP] Post data");
            }
            int httpResponseCode = https.POST(ptr);
        
            if (httpResponseCode > 0) {
              String response = https.getString();
              Serial.printf("[HTTP] POST: %d\n", httpResponseCode);
              sent = true;
            } else {
              Serial.printf("[HTTP] ERROR: %d\n", httpResponseCode);
              ++sendFailures;
            }
        
            https.end();
          } else {
            ++sendFailures;
            Serial.println("[HTTP] Unable to connect");
          }

          if (sent) {
            unsigned bef = ESP.getFreeHeap();
            sendFailures = 0;
            messageQueue.pop();
            delete[] ptr;
            unsigned aft = ESP.getFreeHeap();
            if (the_mesh.debugPrint()) {
              Serial.printf("free mem: %u -> %u >> %d\n",bef,aft,bef-aft);
            }
          }
        }
      }
    } else if (connected && (millis() > (lastConencted + 5000) || sendFailures > 5)) {
      connected = false;
      sendFailures = 0;
      char sender[(PUB_KEY_SIZE * 2) + 1];
      mesh::Utils::toHex(sender, the_mesh.getPubKey(), PUB_KEY_SIZE);

      Serial.println("Reconenct wifi...");

      // if WiFi is down, try reconnecting
      if (!sendsys) {
        JsonDocument doc;
        doc["version"] = 1;
        doc["type"] = "SYS";
        doc["reporter"] = sender;
        doc["time"]["local"] = getTimestamp(); //()->getCurrentTime();
        doc["sys"]["type"] = "wifi";
        doc["sys"]["message"] = "Reconnecting to WiFi...";
        doc["sys"]["heap_total"] = ESP.getHeapSize();
        doc["sys"]["heap_free"] = ESP.getFreeHeap();
        doc["sys"]["uptime"] = millis();
        messageQueue.push(doc);
        sendsys = true;
      }
  
      WiFi.disconnect();
      WiFi.reconnect();
      lastConencted = millis();
    }

    task_sleep(1000);
  }
}

void startWifiTask(int core) {
  xTaskCreatePinnedToCore(
    WiFiTaskCode,   /* Task function. */
      "WiFiTask",     /* name of task. */
      10000,          /* Stack size of task */
      NULL,           /* parameter of the task */
      1,              /* priority of the task */
      &WiFiTask,      /* Task handle to keep track of created task */
      core);          /* pin task to core */
}

void setup() {
  Serial.begin(115200);

  delay(1000);

  board.begin();

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

  SPIFFS.begin(true);
  the_mesh.begin(SPIFFS);

  radio_set_params(the_mesh.getFreqPref(), LORA_BW, LORA_SF, LORA_CR);
  radio_set_tx_power(the_mesh.getTxPowerPref());

  the_mesh.showWelcome();

  // send out initial Advertisement to the mesh
  the_mesh.sendSelfAdvert(1200);   // add slight delay

  int core = xPortGetCoreID() == 1 ? 0 : 1;
  startWifiTask(core);
}

void loop() {
  the_mesh.loop();
}
