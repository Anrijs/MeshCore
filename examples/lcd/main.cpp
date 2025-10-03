#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#ifndef NRF52_PLATFORM
    #error "Only NRF52 supported"
#endif

#include <InternalFileSystem.h>

#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/IdentityStore.h>
#include <RTClib.h>
#include <target.h>

#include <TFT_eSPI.h>
#include <SPI.h>
#include "menu.h"

#include <helpers/BaseChatMesh.h>

#define SEND_TIMEOUT_BASE_MILLIS          500
#define FLOOD_SEND_TIMEOUT_FACTOR         16.0f
#define DIRECT_SEND_PERHOP_FACTOR         6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS   250

#define  PUBLIC_GROUP_PSK  "izOH6cXN6mrJ5e26oRXNcg=="

std::vector<message> messages;
std::vector<message> outmessages;


TFT_eSPI tft = TFT_eSPI();
TFT_eSprite screen = TFT_eSprite(&tft);
GUI gui(&tft);

struct NodePrefs {  // persisted to file
  float airtime_factor;
  char node_name[32];
  double node_lat, node_lon;
  float freq;
  uint8_t tx_power_dbm;
  uint8_t unused[3];
} prefs;

struct {
  struct {
    Menu* m = new Menu(&gui, prefs.node_name);
    MIPage* contacts;
    MIPage* channels;
    MIPage* settings;
    MIAction* flood;
  } home;

  struct {
    Menu* m = new Menu(&gui, "Settings");
    MIPage* minfo;
    MIPage* mradio;
    MIAction* mireboot;
    struct {
      Menu* m = new Menu(&gui, "Public Info");
      MIString* name;
      MIString* pubkey;
      MIBool* gps;
      MIFloat* lat;
      MIFloat* lon;
    } info;
    struct {
      Menu* m = new Menu(&gui, "Radio Settings");
      MIFloat* freq;
      MIFloat* bw;
      MIInteger* sf;
      MIInteger* cr;
      MIInteger* tx;
    } radio;
  } settings;

  struct {
    Menu* m = new Menu(&gui, "Contacts");
  } contacts;

  struct {
    Menu* m = new Menu(&gui, "Channels");
  } channels;
} menu;


String utf8ascii(String s) {      
  String r= "";
  int len = s.length();
  for (int i=0;i<len;i++) {
    char c = s.charAt(i);
    if ((c > 31 && c < 127) || c == '\n') {
      r += c;
    } else if (c == '\t') {
      r += ' ';
    } else {
      uint8_t blen = 0;
      if ((c & 0x80) == 0x00) blen = 1;        // 0xxxxxxx
      else if ((c & 0xE0) == 0xC0) blen = 2;   // 110xxxxx
      else if ((c & 0xF0) == 0xE0) blen = 3;   // 1110xxxx
      else if ((c & 0xF8) == 0xF0) blen = 4;   // 11110xxx

      if (i + blen > len) break; // not enough bytes

      uint32_t utf8code = c << 24;
      int8_t shift = 16;
      while (blen > 1 && shift >= 0) {
        utf8code |= (s.charAt(++i) << shift);
        shift -= 8;
        blen--;
      }

      if (utf8code == 0xC4810000) r+= 'a';
      else if (utf8code == 0xC4800000) r+= 'A';
      else if (utf8code == 0xC4930000) r+= 'e';
      else if (utf8code == 0xC4920000) r+= 'E';
      else if (utf8code == 0xC4AB0000) r+= 'i';
      else if (utf8code == 0xC4AA0000) r+= 'I';
      else if (utf8code == 0xC4B70000) r+= 'k';
      else if (utf8code == 0xC4B60000) r+= 'K';
      else if (utf8code == 0xC4BC0000) r+= 'l';
      else if (utf8code == 0xC4BB0000) r+= 'L';
      else if (utf8code == 0xC48D0000) r+= 'c';
      else if (utf8code == 0xC48C0000) r+= 'C';
      else if (utf8code == 0xC5AB0000) r+= 'u';
      else if (utf8code == 0xC5AA0000) r+= 'U';
      else if (utf8code == 0xC5A10000) r+= 's';
      else if (utf8code == 0xC5A00000) r+= 'S';
      else if (utf8code == 0xC5BE0000) r+= 'z';
      else if (utf8code == 0xC5BD0000) r+= 'Z';
      else if (utf8code == 0xC5860000) r+= 'n';
      else if (utf8code == 0xC5850000) r+= 'N';
    
      // TODO: add custom emoji font????
      else if (utf8code == 0xF09F9880) r+= ":)";
      else if (utf8code == 0xF09F9881) r+= ":D";
      else if (utf8code == 0xF09F9882) r+= ":'D";
      else if (utf8code == 0xF09F9883) r+= ":)";
      else if (utf8code == 0xF09F9884) r+= ":D";
      else if (utf8code == 0xF09F9885) r+= ":'D";
      else if (utf8code == 0xF09F988E) r+= "8)";

      else { Serial.printf("-- Unknown code: %08X\n", utf8code);}
    }
  }
  return r;
}


/* -------------------------------------------------------------------------------------- */

class MyMesh : public BaseChatMesh, ContactVisitor {
  FILESYSTEM* _fs;
  uint32_t expected_ack_crc;
  ChannelDetails* _public;
  unsigned long last_msg_sent;
  ContactInfo* curr_recipient;
  char command[512+10];
  uint8_t tmp_buf[256];
  char hex_buf[512];

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

          
          MILabel* micon = new MILabel(&gui, c.name);
          //MIPage* mipri = new MIPage(&gui, "MC Tikla Paplasinasana", pri);
          menu.contacts.m->add(micon);

          c.id = mesh::Identity(pub_key);
          c.lastmod = 0;
          if (!addContact(c)) full = true;
        }
        file.close();
      }
    }
  }

  void saveContacts() {
#if defined(NRF52_PLATFORM)
    _fs->remove("/contacts");
    File file = _fs->open("/contacts", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = _fs->open("/contacts", "w");
#else
    File file = _fs->open("/contacts", "w", true);
#endif
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

  void setClock(uint32_t timestamp) {
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (timestamp > curr) {
      getRTCClock()->setCurrentTime(timestamp);
      Serial.println("   (OK - clock set!)");
    } else {
      Serial.println("   (ERR: clock cannot go backwards)");
    }
  }

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
    return prefs.airtime_factor;
  }

  int calcRxDelay(float score, uint32_t air_time) const override {
    return 0;  // disable rxdelay
  }

  bool allowPacketForward(const mesh::Packet* packet) override {
    return true;
  }

  void onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) {
    BaseChatMesh::onAdvertRecv(pkt, id, timestamp, app_data, app_data_len);  // chain to super impl
    if (getRTCClock()->getCurrentTime() < timestamp) {
      Serial.printf("Change time %u -> %u\n", getRTCClock()->getCurrentTime(), timestamp);
      getRTCClock()->setCurrentTime(timestamp);
    }
  }

  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override {
    // TODO: if not in favs,  prompt to add as fav(?)

    Serial.printf("ADVERT from -> %s\n", contact.name);
    Serial.printf("  type: %s\n", getTypeName(contact.type));
    Serial.print("   public key: "); mesh::Utils::printHex(Serial, contact.id.pub_key, PUB_KEY_SIZE); Serial.println();

    saveContacts();
  }

  void onContactPathUpdated(const ContactInfo& contact) override {
    Serial.printf("PATH to: %s, path_len=%d\n", contact.name, (int32_t) contact.out_path_len);
    saveContacts();
  }

  ContactInfo* processAck(const uint8_t *data) override {
    if (memcmp(data, &expected_ack_crc, 4) == 0) {     // got an ACK from recipient
      Serial.printf("   Got ACK! (round trip: %d millis)\n", _ms->getMillis() - last_msg_sent);
      // NOTE: the same ACK can be received multiple times!
      expected_ack_crc = 0;  // reset our expected hash, now that we have received ACK
      return NULL;  // TODO: really should return ContactInfo pointer 
    }

    //uint32_t crc;
    //memcpy(&crc, data, 4);
    //MESH_DEBUG_PRINTLN("unknown ACK received: %08X (expected: %08X)", crc, expected_ack_crc);
    return NULL;
  }

  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
    Serial.printf("(%s) MSG -> from %s\n", pkt->isRouteDirect() ? "DIRECT" : "FLOOD", from.name);
    Serial.printf("   %s\n", text);

    if (strcmp(text, "clock sync") == 0) {  // special text command
      setClock(sender_timestamp + 1);
    }
  }

  void onCommandDataRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
  }
  void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) override {
  }
  
  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) override {
    if (pkt->isRouteDirect()) {
      Serial.printf("PUBLIC CHANNEL MSG -> (Direct!)\n");
    } else {
      Serial.printf("PUBLIC CHANNEL MSG -> (Flood) hops %d\n", pkt->path_len);
    }
    
    String in = text;
    String out = utf8ascii(text);

    messages.push_back(message(out.c_str(), 0, 0, false));
    gui.draw(true);

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
  char battstr[16] = "";
  char uptimestr[16] = "0d 00:00:00";

  MyMesh(mesh::Radio& radio, StdRNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables)
     : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables)
  {
    // defaults
    memset(&prefs, 0, sizeof(prefs));
    prefs.airtime_factor = 2.0;    // one third
    strcpy(prefs.node_name, "Klucis");
    prefs.freq = LORA_FREQ;
    prefs.tx_power_dbm = LORA_TX_POWER;

    command[0] = 0;
    curr_recipient = NULL;
  }

  float getFreqPref() const { return prefs.freq; }
  uint8_t getTxPowerPref() const { return prefs.tx_power_dbm; }

  void begin(FILESYSTEM& fs) {
    _fs = &fs;

    BaseChatMesh::begin();

    IdentityStore store(fs, "");

    if (!store.load("_main", self_id, prefs.node_name, sizeof(prefs.node_name))) {  // legacy: node_name was from identity file
      // Need way to get some entropy to seed RNG
      Serial.println("Press ENTER to generate key:");
      char c = 0;
      while (c != '\n') {   // wait for ENTER to be pressed
        if (Serial.available()) c = Serial.read();
      }
      ((StdRNG *)getRNG())->begin(millis());

      self_id = mesh::LocalIdentity(getRNG());  // create new random identity
      int count = 0;
      while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
        self_id = mesh::LocalIdentity(getRNG()); count++;
      }
      store.save("_main", self_id);
    }

    // load persisted prefs
    if (_fs->exists("/node_prefs")) {
      File file = _fs->open("/node_prefs");
      if (file) {
        file.read((uint8_t *) &prefs, sizeof(prefs));
        file.close();
      }
    }

    loadContacts();
    _public = addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel
  }

  void savePrefs() {
#if defined(NRF52_PLATFORM)
    _fs->remove("/node_prefs");
    File file = _fs->open("/node_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = _fs->open("/node_prefs", "w");
#else
    File file = _fs->open("/node_prefs", "w", true);
#endif
    if (file) {
      file.write((const uint8_t *)&prefs, sizeof(prefs));
      file.close();
    }
  }

  void sendSelfAdvert(int delay_millis) {
    auto pkt = createSelfAdvert(prefs.node_name, prefs.node_lat, prefs.node_lon);
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

  void loop() {
    static long batRead = 0;
    static long guiUpdate = 0;
    if (millis() > guiUpdate && gui.isOn()) {
      if (millis() > batRead) {
        uint16_t mv = board.getBattMilliVolts();
        sprintf(battstr, "%.2f V", (mv / 1000.0));
        batRead = millis() + 10000;
      }

      long totalSeconds = millis() / 1000;
      int days    = totalSeconds / 86400;
      int hours   = (totalSeconds % 86400) / 3600;
      int minutes = (totalSeconds % 3600) / 60;
      int seconds = totalSeconds % 60;
        
      sprintf(uptimestr, "%dd %02d:%02d:%02d", days, hours, minutes, seconds);
      guiUpdate = millis() + 1000;
    }
    
    BaseChatMesh::loop();

    if (Serial.available()) {
      while (Serial.available()) {
        char c = Serial.read();
        Serial.printf("usb in: %02X\n", (int) c);
        gui.onInput(c);
      }
      gui.draw();
    }

    if (Serial2.available()) {
      Serial.print("keypad: ");
      while (Serial2.available()) {
        char c = Serial2.read();
        Serial.print(c);
        gui.onInput(c);
      }
      Serial.println();
      gui.draw();
    }

    gui.loop(); // for t9 input draw

    if (outmessages.size() > 0) {
      message m = outmessages.front();
      outmessages.pop_back();
      messages.push_back(m);

      uint8_t temp[5+MAX_TEXT_LEN+32];
      uint32_t timestamp = getRTCClock()->getCurrentTime();
      memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
      temp[4] = 0;  // attempt and flags

      sprintf((char *) &temp[5], "%s: %s", prefs.node_name, m.msg.c_str());  // <sender>: <msg>
      temp[5 + MAX_TEXT_LEN] = 0;  // truncate if too long

      int len = strlen((char *) &temp[5]);
      auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, _public->channel, temp, 5 + len);
      if (pkt) {
        sendFlood(pkt);
        Serial.println("   Sent.");
      } else {
        Serial.println("   ERROR: unable to send");
      }
      gui.draw(true);
    }
  }
};

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, *new VolatileRTCClock(), tables); // TODO: test with 'rtc_clock' in target.cpp

void halt() {
  while (1) ;
}

bool miActionFlood(MIAction* action) {
  the_mesh.sendSelfAdvert(100);
  return true;
}

bool miActionSaveAndReboot(MIAction* action) {
  the_mesh.savePrefs();
  delay(100);
  NVIC_SystemReset();
  return true;
}

bool miActionBack(MIAction* action) {
  action->getGUI()->pop();
  return true;
}

bool miActionBrightnessP(MIAction* action) {
  int step = 8;
  int brightness = gui.getBrightness();
  if (brightness < 16) step = 1;
  else if (brightness < 32) step = 2;
  else if (brightness < 64) step = 4;
  gui.setBrightness(brightness + step);
}

bool miActionBrightnessM(MIAction* action) {
  int step = 8;
  int brightness = gui.getBrightness();
  if (brightness < 16) step = 1;
  else if (brightness < 32) step = 2;
  else if (brightness < 64) step = 4;
  gui.setBrightness(brightness - step);
}

void setupMenu() {
  /** Home **/
  menu.home.channels = new MIPage(&gui, "Channels", menu.channels.m);
  menu.home.contacts = new MIPage(&gui, "Contacts", menu.contacts.m);
  menu.home.settings = new MIPage(&gui, "Settings", menu.settings.m);
  menu.home.flood = new MIAction(&gui, "Flood", &miActionFlood);
  menu.home.m->add(menu.home.channels);
  menu.home.m->add(menu.home.contacts);
  menu.home.m->add(menu.home.settings);
  menu.home.m->add(menu.home.flood);
  menu.home.m->add(new MIString(&gui, "Battery", the_mesh.battstr, 15));
  menu.home.m->add(new MIString(&gui, "Uptime", the_mesh.uptimestr, 15));


  // dev stuff
  Keeb* devkeeb = new Keeb(&gui);
  MIPage* devp1 = new MIPage(&gui, "Keyboard", devkeeb);
  //menu.home.m->add(devp1);
  
  /** Settings **/
  menu.settings.minfo = new MIPage(&gui, "Public Info", menu.settings.info.m);
  menu.settings.mradio = new MIPage(&gui, "Radio", menu.settings.radio.m);
  menu.settings.mireboot = new MIAction(&gui, "Save & Reboot", &miActionSaveAndReboot);
  menu.settings.m->add(menu.settings.minfo);
  menu.settings.m->add(menu.settings.mradio);
  menu.settings.m->add(menu.settings.mireboot);
  menu.settings.m->add(new MIAction(&gui, "Bright+", &miActionBrightnessP));
  menu.settings.m->add(new MIAction(&gui, "Bright-", &miActionBrightnessM));

  
  /** Settings - Info **/
  menu.settings.info.name = new MIString(&gui, "Name", prefs.node_name, 32);
  //menu.settings.info.name->maxlen = 31;
  //menu.settings.info.pubkey = new MIString(&gui, "Public Key", contact.id.pub_key, PUB_KEY_SIZE); // mesh::Utils::printHex(Serial, contact.id.pub_key, PUB_KEY_SIZE)
  //menu.settings.info.pubkey->ro = true;
  menu.settings.info.lat = new MIFloat(&gui, "Lat", &prefs.node_lat, 5);
  menu.settings.info.lon = new MIFloat(&gui, "Lon", &prefs.node_lon, 5);
  //menu.settings.info.gps = new MIBool(&gui, "Use GPS", &nodePrefs.gps);
  menu.settings.info.m->add(menu.settings.info.name);
  //menu.settings.info.m->add(menu.settings.info.pubkey);
  menu.settings.info.m->add(new MISeparator(&gui));
  menu.settings.info.m->add(menu.settings.info.lat);
  menu.settings.info.m->add(menu.settings.info.lon);

  //menu.settings.info.m->add(menu.settings.info.gps);

  /** Settings - Radio **/
  menu.settings.radio.freq = new MIFloat(&gui, "Frequency", &prefs.freq);
  //menu.settings.radio.bw = new MIFloat(&gui, "Bandwidth", &nodePrefs.bw);
  //menu.settings.radio.sf = new MIInteger(&gui, "Spreading Factor", &nodePrefs.sf);
  //menu.settings.radio.sf->setRange(6, 12);
  //menu.settings.radio.cr = new MIInteger(&gui, "Coding Rate", &nodePrefs.cr);
  //menu.settings.radio.cr->setRange(5, 8);
  menu.settings.radio.tx = new MIInteger(&gui, "Tx Power (dBm)", &prefs.tx_power_dbm);
  //menu.settings.radio.tx->setRange(1, 22);

  menu.settings.radio.m->add(menu.settings.radio.freq);
  //menu.settings.radio.m->add(menu.settings.radio.bw);
  //menu.settings.radio.m->add(menu.settings.radio.sf);
  //menu.settings.radio.m->add(menu.settings.radio.cr);
  menu.settings.radio.m->add(menu.settings.radio.tx);

  // Testing
  // messages.push_back(message("Normis: Labrit velreiz visiem!", 11, 41, false));
  // messages.push_back(message("Normis: Baldone is online", 11, 41, false));
  // messages.push_back(message("ALX Mobile: Labrit", 11, 41, false));
  // messages.push_back(message("Nrcha Mobile: Hello! Vai tad pirms tam nebija online?", 11, 43, false));
  // messages.push_back(message("Nrcha Mobile: Un kur Carnikava pazuda?", 11, 43, false));
  // messages.push_back(message("Normis: Tas bija tik sen, ka neskaitas", 11, 44, false));

  Channel* pub = new Channel(&gui, &messages, &outmessages);
  Channel* pri = new Channel(&gui, &messages, &outmessages);
  MIPage* mipub = new MIPage(&gui, "Public", pub);
  //MIPage* mipri = new MIPage(&gui, "MC Tikla Paplasinasana", pri);
  menu.channels.m->add(mipub);
  //menu.channels.m->add(mipri);
}

void setup() {
  pinMode(TFT_BL, OUTPUT);
  gui.setBrightness(0);
  
  Serial.begin(115200);
  Serial2.begin(9600);

  board.begin();

  SPI1.setPins(TFT_MISO, TFT_SCLK, TFT_MOSI);
  SPI1.begin();
  tft.getSPIinstance();
  tft.init();
  tft.setRotation(3);

  setupMenu();
  pinMode(BTN_WAKE, INPUT);
  gui.setBrightness(16); // 11 bad 

  Boot* hello = new Boot(&gui);

  gui.page = hello;
  gui.draw();

  if (!radio_init()) { halt(); }

  Serial.println("Radio");
  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  the_mesh.begin(InternalFS);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  the_mesh.begin(LittleFS);
#elif defined(ESP32)
  SPIFFS.begin(true);
  the_mesh.begin(SPIFFS);
#else
  #error "need to define filesystem"
#endif

  radio_set_params(the_mesh.getFreqPref(), LORA_BW, LORA_SF, LORA_CR);
  radio_set_tx_power(the_mesh.getTxPowerPref());
  // send out initial Advertisement to the mesh
  // the_mesh.sendSelfAdvert(1200);   // add slight delay

  gui.page = menu.home.m;
  gui.draw();
  delete hello;
}

String hexy(const char* in) {
  String out;
  char hex[6];
  for (int i=0;i<strlen(in);i++) {
    if (i > 0) out += ":";
    sprintf(hex, "%02X", in[i]);
    out += hex;
  }
  return out;
}


void loop() {
  the_mesh.loop();
  delay(2);

  if (digitalRead(BTN_WAKE) == 0) {
    if (gui.isOn()) {
      gui.sleep();
    } else {
      gui.wakeup();
    }
    while (digitalRead(BTN_WAKE) == 0) {
      delay(10);
    }
  }
}
