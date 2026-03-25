#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#ifndef NRF52_PLATFORM
    #error "Only NRF52 supported"
#endif


#include <RTClib.h>
#include <target.h>

#include <TFT_eSPI.h>
#include <SPI.h>
#include "MyMesh.h"
#include "utf2ascii.h"
#include "MeshTables.h"
#include <helpers/BaseChatMesh.h>

#include "secret.h"

std::vector<message> messages;
std::vector<message> outmessages;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite screen = TFT_eSprite(&tft);
GUI gui(&tft, &messages, &outmessages);

StdRNG fast_rng;
LcdMeshTables tables(&gui);
MyMesh the_mesh(radio_driver, fast_rng, *new VolatileRTCClock(), tables, &gui, &messages, &outmessages);

char battstr[16] = "";
char nfstr[16] = "";
char uptimestr[16] = "0d 00:00:00";

struct {
  struct {
    Menu* m = new Menu(&gui, the_mesh._prefs.node_name);
    MIPage* contacts;
    MIPage* channels;
    MIPage* settings;
    MIAction* flood;
    MIAction* advert;
    MIString* nf;
    MIString* batt;
    MIString* uptime;
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

/* -------------------------------------------------------------------------------------- */


void halt() {
  while (1) {
    Serial.println("HALTED.");
    delay(5000);
  }
}

bool miActionFlood(MIAction* action) {
  the_mesh.sendSelfAdvert(100, true);
  return true;
}

bool miActionAdvert(MIAction* action) {
  the_mesh.sendSelfAdvert(100, false);
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
  return true;
}

bool miActionBrightnessM(MIAction* action) {
  int step = 8;
  int brightness = gui.getBrightness();
  if (brightness < 16) step = 1;
  else if (brightness < 32) step = 2;
  else if (brightness < 64) step = 4;
  gui.setBrightness(brightness - step);
  return true;
}

bool miActionRotate(MIAction* action) {
  static bool horizontal = true;
  horizontal = !horizontal;
  tft.setRotation(horizontal ? 3 : 0);
  gui.page->invalidate();
  gui.draw();
  return true;
}

void setupMenu() {
  /** Home **/
  menu.home.channels = new MIPage(&gui, "Channels", menu.channels.m);
  menu.home.contacts = new MIPage(&gui, "Contacts", menu.contacts.m);
  menu.home.settings = new MIPage(&gui, "Settings", menu.settings.m);
  menu.home.nf = new MIString(&gui, "Noise Floor", nfstr, 15);
  menu.home.batt = new MIString(&gui, "Battery", battstr, 15);
  menu.home.uptime = new MIString(&gui, "Uptime", uptimestr, 15);


  menu.home.flood = new MIAction(&gui, "Adv. Flood", &miActionFlood);
  menu.home.advert = new MIAction(&gui, "Adv. Direct", &miActionAdvert);
  menu.home.m->add(menu.home.channels);
  menu.home.m->add(menu.home.contacts);
  menu.home.m->add(menu.home.settings);
  menu.home.m->add(menu.home.flood);
  menu.home.m->add(menu.home.advert);
  menu.home.m->add(menu.home.nf);
  menu.home.m->add(menu.home.batt);
  menu.home.m->add(menu.home.uptime);

  // dev
  MIAction* rot = new MIAction(&gui, "Rotate", &miActionRotate);
  menu.home.m->add(rot);

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
  menu.settings.info.name = new MIString(&gui, "Name", the_mesh._prefs.node_name, 32);
  //menu.settings.info.name->maxlen = 31;
  //menu.settings.info.pubkey = new MIString(&gui, "Public Key", contact.id.pub_key, PUB_KEY_SIZE); // mesh::Utils::printHex(Serial, contact.id.pub_key, PUB_KEY_SIZE)
  //menu.settings.info.pubkey->ro = true;
  //menu.settings.info.lat = new MIFloat(&gui, "Lat", &the_mesh._prefs.node_lat, 5); // whatever. it shouldnt use it anyway]
  //menu.settings.info.lon = new MIFloat(&gui, "Lon", &the_mesh._prefs.node_lon, 5);
  //menu.settings.info.gps = new MIBool(&gui, "Use GPS", &nodePrefs.gps);
  menu.settings.info.m->add(menu.settings.info.name);
  //menu.settings.info.m->add(menu.settings.info.pubkey);
  menu.settings.info.m->add(new MISeparator(&gui));
  menu.settings.info.m->add(menu.settings.info.lat);
  menu.settings.info.m->add(menu.settings.info.lon);

  //menu.settings.info.m->add(menu.settings.info.gps);

  /** Settings - Radio **/
  menu.settings.radio.freq = new MIFloat(&gui, "Frequency", &the_mesh._prefs.freq);
  //menu.settings.radio.bw = new MIFloat(&gui, "Bandwidth", &nodePrefs.bw);
  //menu.settings.radio.sf = new MIInteger(&gui, "Spreading Factor", &nodePrefs.sf);
  //menu.settings.radio.sf->setRange(6, 12);
  //menu.settings.radio.cr = new MIInteger(&gui, "Coding Rate", &nodePrefs.cr);
  //menu.settings.radio.cr->setRange(5, 8);
  menu.settings.radio.tx = new MIInteger(&gui, "Tx Power (dBm)", &the_mesh._prefs.tx_power_dbm);
  //menu.settings.radio.tx->setRange(1, 22);

  menu.settings.radio.m->add(menu.settings.radio.freq);
  //menu.settings.radio.m->add(menu.settings.radio.bw);
  //menu.settings.radio.m->add(menu.settings.radio.sf);
  //menu.settings.radio.m->add(menu.settings.radio.cr);
  menu.settings.radio.m->add(menu.settings.radio.tx);
}

void setup() {
  pinMode(BTN_WAKE, INPUT);
  Serial.begin(115200);
  Serial1.begin(9600);

  delay(100);

  tft.init();
  tft.setRotation(3);
  tft.setAttribute(UTF8_SWITCH, 0);

  delay(100);

  Boot* hello = new Boot(&gui);
  gui.page = hello;

  gui.draw();
  gui.setBrightness(16);

  board.begin();
  setupMenu();

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

  InternalFS.begin();
  the_mesh.begin(InternalFS);
  the_mesh.getRTCClock()->setCurrentTime(BUILD_TIME_EPOCH);

  // ch must be crated after mesh
  Channel* pub = new Channel(&gui, the_mesh._public);
  MIPage* mipub = new MIPage(&gui, "Public", pub);
  menu.channels.m->add(mipub);

  addSecrets(&the_mesh, &gui, menu.channels.m);

  radio_set_params(the_mesh.getFreqPref(), LORA_BW, LORA_SF, LORA_CR);
  radio_set_tx_power(the_mesh.getTxPowerPref());

  gui.page = menu.home.m;
  gui.draw();
  delete hello;

    // get all contacts
  ContactsIterator iter = the_mesh.startContactsIterator();
  ContactInfo contact;

  while (iter.hasNext(&the_mesh, contact)) {
    if (contact.type != ADV_TYPE_CHAT) continue;

    ContactInfo* ci = the_mesh.lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE);
    Serial.printf("Add %s\n", ci->name);
    Channel* con = new Channel(&gui, ci);
    MIPage* micon = new MIPage(&gui, ci->name, con);
    menu.contacts.m->add(micon);
  }
}

void loop() {
  static long batRead = 0;
  static long guiUpdate = 0;
  
  the_mesh.loop();

  if (millis() > guiUpdate && gui.isOn()) {
    if (millis() > batRead) {
      uint16_t mv = board.getBattMilliVolts();
      sprintf(battstr, "%.2f V", (mv / 1000.0));
      batRead = millis() + 10000;
      menu.home.batt->invalidate();
    }

    long totalSeconds = millis() / 1000;
    int days    = totalSeconds / 86400;
    int hours   = (totalSeconds % 86400) / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    sprintf(nfstr, "%d", radio_driver.getNoiseFloor());
    sprintf(uptimestr, "%dd %02d:%02d:%02d", days, hours, minutes, seconds);
    guiUpdate = millis() + 1000;
    menu.home.uptime->invalidate();
    menu.home.nf->invalidate();

    if (gui.page == menu.home.m) {
      menu.home.m->draw();
    }
  }

  if (Serial.available()) {
    while (Serial.available()) {
      char c = Serial.read();
      Serial.printf("usb in: %02X\n", (int) c);
      gui.onInput(c);
    }
    gui.draw();
  }

  if (Serial1.available()) {
    Serial.print("keypad: ");
    while (Serial1.available()) {
      char c = Serial1.read();
      Serial.print(c);
      gui.onInput(c);
    }
    Serial.println();
    gui.draw();
  }

  gui.loop();

  if (the_mesh.outmessages->size() > 0) {
    message m = the_mesh.outmessages->front();
    the_mesh.outmessages->pop_back();

    uint8_t temp[5+MAX_TEXT_LEN+32];
    uint32_t timestamp = the_mesh.getRTCClock()->getCurrentTime();
    memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
    temp[4] = 0;  // attempt and flags

    if (m.ch) {
      sprintf((char *) &temp[5], "%s: %s", the_mesh._prefs.node_name, m.msg.c_str());  // <sender>: <msg>
    } else {
      sprintf((char *) &temp[5], "%s", m.msg.c_str());  // <sender>: <msg>
    }
    temp[5 + MAX_TEXT_LEN] = 0;  // truncate if too long

    DateTime dt(timestamp + the_mesh.gmtOffset);
    m.hh = dt.hour();
    m.mm = dt.minute();

    int len = strlen((char *) &temp[5]);
    mesh::Packet* pkt;
    if (m.ch) {
      pkt = the_mesh.createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, m.ch->channel, temp, 5 + len);
    } else if (m.ci) {
      pkt = the_mesh.createDatagram(PAYLOAD_TYPE_TXT_MSG, m.ci->id, m.ci->getSharedSecret(the_mesh.self_id), temp, 5 + len); // createGroupDatagram(PAYLOAD_TYPE_TXT_MSG, m.ci, temp, 5 + len);
    }
    
    if (pkt) {
      m.setHash(pkt);
      m.repeats--; // reduce seen by sending
      appendMessage(*gui.messages, m);
      the_mesh.sendFlood(pkt);
      Serial.println("   Sent.");
    } else {
      Serial.println("   ERROR: unable to send");
    }
    gui.draw(true);
  }

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

  delay(2);
}
