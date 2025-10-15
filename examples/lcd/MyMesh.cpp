#include "MyMesh.h"
#include "utf2ascii.h"

MyMesh::MyMesh(mesh::Radio& radio, StdRNG& rng, mesh::RTCClock& rtc, LcdMeshTables& tables, GUI* gui, std::vector<message>* messages, std::vector<message>* outmessages)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables),
      messages(messages),
      outmessages(outmessages),
      gui(gui) {
  // defaults
  memset(&_prefs, 0, sizeof(_prefs));
  _prefs.airtime_factor = 2.0;    // one third
  strcpy(_prefs.node_name, "Klucis");
  _prefs.freq = LORA_FREQ;
  _prefs.tx_power_dbm = LORA_TX_POWER;

  command[0] = 0;
  curr_recipient = NULL;
}

void MyMesh::begin(FILESYSTEM& fs) {
  _fs = &fs;

  BaseChatMesh::begin();

  IdentityStore store(fs, "");

  if (!store.load("_main", self_id, _prefs.node_name, sizeof(_prefs.node_name))) {  // legacy: node_name was from identity file
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
      file.read((uint8_t *) &_prefs, sizeof(_prefs));
      file.close();
    }
  }

  loadContacts();
  _public = addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel
  channels.push_back(_public);
}

void MyMesh::savePrefs() {
  _fs->remove("/node_prefs");
  File file = _fs->open("/node_prefs", FILE_O_WRITE);

  if (file) {
    file.write((const uint8_t *)&_prefs, sizeof(_prefs));
    file.close();
  }
}

void MyMesh::loadContacts() {
  if (!_fs->exists("/contacts")) return;

  File file = _fs->open("/contacts");
  if (!file) return;

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

void MyMesh::saveContacts() {
  _fs->remove("/contacts");
  File file = _fs->open("/contacts", FILE_O_WRITE);
  if (!file) return;

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

void MyMesh::setClock(uint32_t timestamp) {
  uint32_t curr = getRTCClock()->getCurrentTime();
  if (timestamp > curr) {
    getRTCClock()->setCurrentTime(timestamp);
    DateTime dt(timestamp + gmtOffset);
    Serial.printf("   (OK - clock set! %02d:%02d:%02d)\n",
      dt.hour(),
      dt.minute(),
      dt.second()
    );
  } else {
    Serial.println("   (ERR: clock cannot go backwards)");
  }
}

void MyMesh::onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) {
  BaseChatMesh::onAdvertRecv(pkt, id, timestamp, app_data, app_data_len);  // chain to super impl
  setClock(timestamp + 1);
}

void MyMesh::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  // TODO: if not in favs,  prompt to add as fav(?)
  Serial.print("   public key: "); mesh::Utils::printHex(Serial, contact.id.pub_key, PUB_KEY_SIZE); Serial.println();
  saveContacts();
}

void MyMesh::onContactPathUpdated(const ContactInfo& contact) {
  Serial.printf("PATH to: %s, path_len=%d\n", contact.name, (int32_t) contact.out_path_len);
  saveContacts();
}

ContactInfo* MyMesh::processAck(const uint8_t *data) {
  Serial.printf("   Got ACK!");
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

void MyMesh::onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) {
  Serial.printf("(%s) MSG -> from %s\n", pkt->isRouteDirect() ? "DIRECT" : "FLOOD", from.name);
  Serial.printf("   %s\n", text);

  if (strcmp(text, "clock sync") == 0) {  // special text command
    setClock(sender_timestamp + 1);
  }

  String out = utf8ascii(text);
  ContactInfo* ci = lookupContactByPubKey(from.id.pub_key, PUB_KEY_SIZE);

  if (ci) {
    DateTime dt(sender_timestamp + gmtOffset);
    messages->push_back(message(ci, out.c_str(), dt.hour(), dt.minute(), false));
    gui->draw(true);
  } else {
    Serial.printf("onMessageRecv: Contact not found\n", from.name);
  }
}

void MyMesh::onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) {
  if (pkt->isRouteDirect()) {
    Serial.printf("PUBLIC CHANNEL MSG -> (Direct!)\n");
  } else {
    Serial.printf("PUBLIC CHANNEL MSG -> (Flood) hops %d\n", pkt->path_len);
  }
    
  String out = utf8ascii(text);
  uint32_t ack;
  memcpy(&ack, pkt->payload, 4);

  ChannelDetails* ch = nullptr;
  for (ChannelDetails* c : channels) {
    if (memcmp(channel.secret, c->channel.secret, PUB_KEY_SIZE) == 0) {
      ch = c;
      break;
    }
  }

  if (ch) {
    DateTime dt(timestamp + gmtOffset);
    messages->push_back(message(ch, out.c_str(), dt.hour(), dt.minute(), false));
    gui->draw(true);
  }
}

void MyMesh::sendSelfAdvert(int delay_millis) {
  auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
  if (pkt) {
    sendFlood(pkt, delay_millis);
  }
}

void MyMesh::onContactVisit(const ContactInfo& contact) {
  Serial.printf("   %s - ", contact.name);
  char tmp[40];
  int32_t secs = contact.last_advert_timestamp - getRTCClock()->getCurrentTime();
  AdvertTimeHelper::formatRelativeTimeDiff(tmp, secs, false);
  Serial.println(tmp);
}
