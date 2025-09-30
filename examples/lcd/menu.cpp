#include "menu.h"

// toto: settings fields should be edited only on enter, hilight enter
void GUI::draw(bool invalidate) {
    wakeup();

    if (!page) {
        tft->fillScreen(MI_COLOR_BKG);
        tft->setTextFont(MI_FONT);
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

    gui->tft->setTextFont(MI_FONT);
    gui->tft->setTextColor(TFT_RED);
    gui->tft->setCursor(0,8);
    gui->tft->setTextWrap(true);
    gui->tft->print("ERROR: Page not implemented");
    onDraw();
}

void Page::onInput(char c) {
    // ??
}

void Menu::draw() {
    uint16_t y = 0;
    int pos = 0;
    gui->tft->setTextWrap(wrap);
    title->draw(y, MI_COLOR_TITLE_BKG);
    for (auto &mi: menuitems) {
        //mi->draw(); // it should have position coocked in
        uint16_t color = MI_COLOR_BKG;
        if (pos == selected) {
            color = mi->editing ? MI_COLOR_EDITING_BKG : MI_COLOR_SELECTED_BKG;
        }
        mi->draw(y, color);
        pos++;
    }
    if (fill && y < gui->tft->height()) {
        gui->tft->fillRect(0, y, gui->tft->width(), gui->tft->height() - y, MI_COLOR_BKG);
        fill = false;
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
    gui->tft->fillScreen(MI_COLOR_BKG);

    gui->tft->setTextSize(3*MI_SCALE);
    gui->tft->setCursor(10, 40);
    gui->tft->print("MeshCore");

    gui->tft->setTextSize(1*MI_SCALE);
    gui->tft->setCursor(20, 67);
    gui->tft->print("MC: v1.7.4, GUI: v0.1");
}

void GUI::pop() {
    if (pages.size > 0) {
        if (page->onPop()) {
            page = pages.stack[pages.size];
            pages.size--;
            //tft->fillScreen(MI_COLOR_BKG);
        }
        page->invalidate();
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
        if ((c >= '0' && c <= '9') || c == '*' || c == '#') {
            int8_t ckey = 0;
            if (c == '*') ckey = 10;
            else if (c == '#') ckey = 11;
            else ckey = c - '0';

            if(t9key >= 0 && t9key != ckey) t9commit();

            if (t9key >= 0) t9pos++;
            if (t9pos >= strlen(t9Chars[ckey])) t9pos = 0;

            t9time = millis();
            t9key = ckey;
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

void GUI::t9commit() {
    onInput(t9Chars[t9key][t9pos]);
    t9time = 0;
    t9pos = 0;
    t9key = -1;

    // TODO: draw on bottom
    uint16_t w = tft->width();
    uint16_t h = tft->height();
    tft->setTextSize(2*MI_SCALE);
    uint16_t th = tft->fontHeight();
    uint16_t p = 4; // text padding
    uint16_t d = th + p + p; // box size
    uint16_t x = (w - d) / 2;
    uint16_t y = (h - d) / 2;
    tft->fillRect(x,y,d,d, TFT_WHITE);
    if (page) page->invalidate();
    draw();
}

void GUI::loop()  {
    static int8_t lastkey = -1;
    static int8_t lastpos = -1;
    // Check T9
    if (t9key >= 0 && t9time > 0) {
        if (millis() >= (t9time + t9timeout)) {
           t9commit();
           lastkey = -1;
           lastpos = -1;
        } else if (t9key >= 0 && (lastkey != t9key || lastpos != t9pos)) {
            t9lastpos = t9pos;
            uint16_t w = tft->width();
            uint16_t h = tft->height();

            tft->setTextSize(2*MI_SCALE);
            uint16_t th = tft->fontHeight();

            uint16_t p = 4; // text padding
            uint16_t d = th + p + p; // box size

            uint16_t x = (w - d) / 2;
            uint16_t y = (h - d) / 2;

            uint16_t fw = tft->textWidth(String(getT9char()));

            tft->fillRect(x,y,d,d, TFT_PURPLE);
            tft->setCursor(x + (d - fw) / 2, y + p);
            tft->setTextColor(TFT_WHITE);
            tft->print(getT9char());
            tft->setTextSize(1*MI_SCALE);
            lastkey = t9key;
            lastpos = t9pos;
        }
    }

    if (lcdOn && millis() > lcdOff) {
        sleep();
    }
}

const char* const GUI::t9Chars[12] = {
    " 0",
    ".,!?1",
    "abcABC2",
    "defDEF3",
    "ghiGHI4",
    "jklJKL5",
    "mnoMNO6",
    "pqrsPQRS7",
    "tuvTUV8",
    "wxyzWXYZ9",
    " +-_@#$%^&*()",
    " []{};<>"
};