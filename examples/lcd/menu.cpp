#include "menu.h"

// toto: settings fields should be edited only on enter, hilight enter
void GUI::draw(bool invalidate) {
    wakeup();

    if (!page) {
        tft->fillScreen(MI_COLOR_BKG);
        tft->setTextColor(TFT_RED);
        tft->setCursor(0,8);
        tft->setTextWrap(true);
        tft->print("ERROR: Invalid page");
    } else {
        if (invalidate) page->invalidate();
        page->draw();
    }
}

void Page::draw() {
    if (!gui || !gui->tft) return;

    gui->tft->setTextColor(TFT_RED);
    gui->tft->setCursor(0,8);
    gui->tft->setTextWrap(true);
    gui->tft->print("ERROR: Page not implemented");
    onDraw();
}

void Page::onInput(char c) {
    // ??
}

void Page::onInput(const char *c) {
    // ??
}

void Menu::draw() {
    uint16_t y = 0;
    int pos = 0;
    gui->tft->setTextWrap(wrap);
    title->draw(y, MI_COLOR_TITLE_BKG);

    int skip = 0;
    if (selected > 7) {
        skip = selected - 7;
        invalidate();
    }

    for (auto &mi: menuitems) {
        //mi->draw(); // it should have position coocked in
        uint16_t color = MI_COLOR_BKG;
        if (pos == selected) color = MI_COLOR_SELECTED_BKG;
        pos++;

        if (skip-- > 0) continue;
        mi->draw(y, color);

        if (y > gui->tft->height()) break;
    }
    if (y < gui->tft->height()) {
        TFT_eSprite row = TFT_eSprite(gui->tft);
        row.createSprite(gui->tft->width(), 16);
        row.fillRect(0, 0, gui->tft->width(), 16, MI_COLOR_BKG);

        while(y < gui->tft->height()) {
            row.pushSprite(0, y);
            y += 16;
        }
    }
    onDraw();
}

void Menu::onInput(char c) {
    // TODO: Update only changed items. MI could have staticly stored Y and Page numbers.
    // Then I ould call mi->draw(tft, bkg); without worying about position;
    // Or I could add "changed flag" to items

    switch (c) {
    case 0x1B:
        menuitems[selected]->invalidate();
        if (--selected < 0) selected = menuitems.size() - 1;
        menuitems[selected]->invalidate();
        if (!menuitems[selected]->isSelectable()) onInput(c);
        break;
    case 0x08:
        menuitems[selected]->invalidate();
        if (++selected >= menuitems.size()) selected = 0;
        menuitems[selected]->invalidate();
        if (!menuitems[selected]->isSelectable()) onInput(c);
        break;
    default:
        menuitems[selected]->onInput(c);
        break;
    }
    
}

void Boot::draw() {
    uint16_t h = 32;
    TFT_eSprite row = TFT_eSprite(gui->tft);
    row.createSprite(gui->tft->width(), h);
    row.fillRect(0, 0, gui->tft->width(), h, 0x10c5);

    uint16_t y = 0;
    while (y < gui->tft->height()) {
        row.pushSprite(0, y);
        y+= h;
    }

    gui->tft->setTextColor(TFT_WHITE);
    gui->tft->loadFont(Roboto_Light_18);

    // Bold text
    gui->tft->setCursor(10, 10);
    gui->tft->print("MeshKey GUI");

    gui->tft->setTextColor(TFT_YELLOW);
    gui->tft->setCursor(10, 30);
    gui->tft->print("MeshCore: v1.9.0, GUI: v0.1");
}

void GUI::pop() {
    if (pages.size > 0) {
        if (page->onPop()) {
            page = pages.stack[pages.size];
            pages.size--;
            //tft->fillScreen(MI_COLOR_BKG);
            page->invalidate();
        }
    }
}

void GUI::onInput(char c) {
    if (!lcdOn) {
        wakeup();
        return;
    }
    if (c == 0x1B) {
        pop();
    } else if (t9mode) {
        // keypad code
        if ((c >= '0' && c <= '9') || c == '*' || c == '#') {
            int8_t ckey = 0;
            if (c == '*') ckey = 10;
            else if (c == '#') ckey = 11;
            else ckey = c - '0';

            if(t9key >= 0 && t9key != ckey) t9commit();

            if (t9key >= 0) t9pos++;

            t9time = millis();
            t9key = ckey;
        } else if (c == 0x08 && t9active()) {
            t9cancel();
        } else if (c == 0x0A && t9active()) {
            t9commit();
            page->onInput(c);
        } else {
            page->onInput(c);
        }
    } else if (page) {
        page->onInput(c);
    }
}

void GUI::stack(Page* next) {
    if (pages.size < 16) {
        pages.size++;
        pages.stack[pages.size] = page;
        page = next;
        //tft->fillScreen(MI_COLOR_BKG);
        page->invalidate();
    }
}

bool GUI::t9active() {
    return t9key >= 0 && t9time > 0;
}

void GUI::t9cancel() {
    bool active = t9active();
    t9time = 0;
    t9pos = -1;
    t9key = -1;

    t9time = 0;
    t9pos = -1;
    t9key = -1;

    // TODO: draw on bottom
    if (active) {
        uint16_t w = tft->width();
        uint16_t h = tft->height();
        uint16_t th = tft->fontHeight();
        uint16_t p = 4; // text padding
        uint16_t d = th + p + p; // box size
        uint16_t x = (w - d) / 2;
        uint16_t y = (h - d) / 2;
        tft->fillRect(x,y,d,d, MI_COLOR_BKG);

        if (page) page->invalidate();
        draw();
    }
}

void GUI::t9commit() {
    t9mode = false;
    if (t9key != -1 && t9pos != -1) {
        const char* keypadStr = getKeypadChar();
        t9key = -1;
        t9pos = -1;
        if (page) { page->onInput(keypadStr); }
    }
    t9mode = true;
    t9cancel();
}

void GUI::loop()  {
    static int8_t lastkey = -1;
    static int8_t lastpos = -1;
    // Check T9
    if (t9active()) {
        if (millis() >= (t9time + t9timeout)) {
           t9commit();
           lastkey = -1;
           lastpos = -1;
        } else if (t9key >= 0 && (lastkey != t9key || lastpos != t9pos)) {
            t9lastpos = t9pos;
            uint16_t w = tft->width();
            uint16_t h = tft->height();

            uint16_t th = tft->fontHeight();

            uint16_t p = 4; // text padding
            uint16_t d = th + p + p; // box size

            uint16_t x = (w - d) / 2;
            uint16_t y = (h - d) / 2;

            TFT_eSprite row = TFT_eSprite(tft);
            row.loadFont(Roboto_Light_18);
            row.createSprite(d, d);
            row.setTextColor(MI_COLOR_T9_FONT);
            row.fillRect(0, 0, d, d, MI_COLOR_T9_BKG);
            uint16_t fw = row.textWidth(String(getKeypadChar()));

            row.setCursor((d - fw) / 2,  + p);
            row.print(getKeypadChar());
            row.pushSprite(x, y);

            lastkey = t9key;
            lastpos = t9pos;
        }
    }

    if (lcdOn && millis() > lcdOff) {
        sleep();
    }
}

String MIPage::getValueString() {
    if (next && next->getType() == PAGE_TYPE_CHANNEL) {
        Channel* ch = (Channel*) next;
        ChannelDetails* channel = ch->getChanel();
        ContactInfo* contact = ch->getContact();
        int unread = 0;

        for (int i = 0; i < gui->messages->size(); i++) {
            message m = gui->messages->at(i);
            if (m.read) continue;
            if (channel && channel == m.ch) unread++;
            if (contact && contact == m.ci) unread++;
        }

        if (unread > 0) {
            return String(unread);
        }
    }
    return "";
}
