#ifndef MENU_H
#define MENU_H

/*

  _______ ____  _____   ____  
 |__   __/ __ \|  __ \ / __ \ 
    | | | |  | | |  | | |  | |
    | | | |  | | |  | | |  | |
    | | | |__| | |__| | |__| |
    |_|  \____/|_____/ \____/ 
                              
 - Use TFT_eSprite for rendering elements.
    It should recuce flickering effect on larger displays.
    This will increase RAM usage.
    Alternatively I would need some custom display driver or maybe paralell interface display?
    

*/

#include <stdint.h>
#include <string>
#include <vector>
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <cstdlib>
#include "font.h"

#define LIGHT_MODE

#ifdef LIGHT_MODE
    #define MI_COLOR_TITLE_BKG TFT_LIGHTGREY
    #define MI_COLOR_BKG  TFT_WHITE
    #define MI_COLOR_SELECTED_BKG TFT_ORANGE
    #define MI_COLOR_EDITING_BKG 0x07ff
    #define MI_COLOR_TEXT TFT_BLACK
    #define MI_COLOR_VALUE TFT_DARKGREY
    #define MI_COLOR_INVALID_VALUE TFT_RED
    #define MI_COLOR_SEPARATOR TFT_LIGHTGREY
#else
    #define MI_COLOR_BKG  TFT_BLACK
    #define MI_COLOR_SELECTED_BKG TFT_DARKCYAN
    #define MI_COLOR_TEXT TFT_WHITE
    #define MI_COLOR_VALUE TFT_CYAN
#endif

#define MI_SCALE 1
#define MI_FONT 2
#define MI_FREE_FONT &LatvFont
#define MI_FONT_HEIGHT (chr_hgt_f16 * MI_SCALE)   // font height
#define MI_FONT_PADDING (2 * MI_SCALE)  // px padding each side
#define MI_VALUE_WIDTH  (40 * MI_SCALE) // px padding each side

class GUI;
class Menu;
class Page;
class MI;

struct message {
    String msg;
    uint8_t hh;
    uint8_t mm;
    bool me;

    message(char* buf, uint8_t hh, uint8_t mm, bool me): msg(buf), hh(hh), mm(mm), me(me) { };
    message(String str, uint8_t hh, uint8_t mm, bool me): msg(str), hh(hh), mm(mm), me(me) { };
};

class GUI {
    struct {
        Page* stack[16]; // TODO: this should be costant
        int size = 0;
    } pages;

    const long t9timeout = 1000;

    static const char* const t9Chars[12];

    bool    t9mode = true; // work in progress.
    long    t9time = 0; // when char will be commited. 0 = not active
    int8_t  t9key  = -1;
    uint8_t t9pos  = 0;
    uint8_t t9lastpos = 0;

    bool lcdOn = true;
    long lcdOff = 0;

    int brightness = 32;
    
    bool isT9Sequence() {
        return t9time > 0 && millis() >= (t9time + t9timeout);
    }

    char getT9char() {
        if (t9key >= 0) {
            return t9Chars[t9key][t9pos];
        }
        return ' ';
    }
public:
    TFT_eSPI* tft;
    Page* page = nullptr;

    GUI(TFT_eSPI* tft) {
        this->tft = tft;
    }

    bool isOn() {
        return lcdOn;
    }

    void wakeup() {
        lcdOff = millis() + 15000;
        if (!lcdOn) {
            tft->writecommand(ST7789_SLPOUT);
            analogWrite(TFT_BL, brightness);
        }
        lcdOn = true;
    }

    void sleep() {
        if (lcdOn) {
            tft->writecommand(ST7789_SLPIN);
            analogWrite(TFT_BL, 0);
        }
        lcdOn = false;
    }

    void setBrightness(int bright) {
        brightness = bright;
        if (brightness < 1) brightness = 1;
        if (brightness > 96) brightness = 96; // hard limit. higher value is too much for integrated 3v3

        if (lcdOn) {
            Serial.printf("Bright: %d\n", brightness);
            analogWrite(TFT_BL, brightness);
        }
    }

    int getBrightness() {
        return brightness;
    }

    void draw(bool invalidate = false);
    void onInput(char c);
    void stack(Page* next);
    void pop();
    void loop();
    void t9commit();
};


/*
    generic GUI element
*/
class GUIElement {
protected:
    bool redraw = true;
    GUI* gui = nullptr;
public:
    GUIElement(GUI* gui) { this->gui = gui; }
    GUI* getGUI() const { return gui; }
    void onDraw() { redraw = false; };
    virtual void invalidate() { redraw = true; }
};

/*
    Basic Menu Item, that may contain value. Listens for input.
*/
class MI: public GUIElement {
public:
    bool editing = false;
    bool ro = false;
    MI(GUI* gui): GUIElement(gui) { }
    virtual void draw(uint16_t &y, uint16_t bkg) { };
    virtual void onInput(char c) { };
    virtual bool isSelectable() { return true; }
};

class MISeparator: public MI {
public:
    MISeparator(GUI* gui): MI(gui) { }

    void draw(uint16_t &y, uint16_t bkg) override {
        uint16_t w = gui->tft->width();
        gui->tft->drawLine(0,y,w,y,MI_COLOR_SEPARATOR);
        y++;
        onDraw();
    }

    bool isSelectable() override {
        return false;
    }
};

class MILabel: public MI {
    char* label;
public:
    MILabel(GUI* gui, const char* label): MI(gui) {
        this->label = new char[strlen(label) + 1]; 
        strcpy(this->label, label);
    }

    void draw(uint16_t &y, uint16_t bkg) override {
        uint16_t w = gui->tft->width();
        uint16_t h = MI_FONT_HEIGHT + MI_FONT_PADDING + MI_FONT_PADDING;
        uint16_t valx = w - MI_VALUE_WIDTH;
        
        TFT_eSprite row = TFT_eSprite(gui->tft);
        row.createSprite(w, h);
        row.setFreeFont(MI_FREE_FONT);
        //row.setTextSize(1*MI_SCALE);
        row.setTextWrap(false);
        row.setCursor(MI_FONT_PADDING, 0);
        row.setTextColor(MI_COLOR_TEXT);
        row.fillRect(0,0,w,h,bkg);
        row.setCursor(MI_FONT_PADDING,MI_FONT_PADDING);
        row.print(label);
        if (redraw) row.pushSprite(0, y);
        y += row.getCursorY() + MI_FONT_HEIGHT + MI_FONT_PADDING;

        onDraw();
    }
};

class MIValue: public MILabel {
protected:
    void* value = nullptr;
    bool invalid = false;
public:
    MIValue(GUI* gui, const char* label, void* value): MILabel(gui, label) { this->value = value; }
    virtual void drawValue(uint16_t &y) { // Default is string value. May override with custom view, like bool switch
        if (!redraw) return;

        uint16_t w = gui->tft->width();

        String str = getValueString();
        uint16_t ww = gui->tft->textWidth(str);
        y += MI_FONT_PADDING;

        gui->tft->setCursor(w - ww - MI_FONT_PADDING, y);
        gui->tft->setTextColor(invalid ? MI_COLOR_INVALID_VALUE : MI_COLOR_VALUE);
        gui->tft->setTextWrap(false);
        gui->tft->print(str);

        if (invalid) { // Thick
            gui->tft->setCursor(w - ww - MI_FONT_PADDING - 1, y);
            gui->tft->print(str);
        }
    }

    virtual String getValueString() { return "not implemented"; }
    void draw(uint16_t &y, uint16_t bkg) override {
        uint16_t y0 = y;
        bool rd = redraw;
        MILabel::draw(y, bkg);
        redraw = rd;
        drawValue(y0);
        onDraw();
    };
};

class MIBool: public MIValue {
public:
    MIBool(GUI* gui, const char* label, bool* value): MIValue(gui, label, (void*) value) {}

    bool getb() { return *(bool*) value; }
    void setb(bool b) { *(bool*) value = b; }

    void drawValue(uint16_t &y) override {
        if (!redraw) return;

        uint16_t w = gui->tft->width();

        uint16_t xw = MI_FONT_HEIGHT * 2;
        uint16_t xe = w - xw - MI_FONT_PADDING; // [* ]
        gui->tft->drawRect(
            xe,
            y,
            xw,
            MI_FONT_HEIGHT,
            MI_COLOR_VALUE
        );

        xe += 1;

        if (getb()) {
            xe += MI_FONT_HEIGHT;
        }

        gui->tft->fillRect(
            xe,
            y+1,
            MI_FONT_HEIGHT - 2,
            MI_FONT_HEIGHT - 2,
            getb() ? MI_COLOR_VALUE : TFT_DARKGREY
        );
    }

    void onInput(char c) override {
        if (ro) return;
        setb(!getb());
        invalidate();
    };

    String getValueString() override {
        return "bool not implemented";
    }
};

#define MI_TEXT_FLAG_TEXT    0x01
#define MI_TEXT_FLAG_NUMBER  0x02
#define MI_TEXT_FLAG_SIGNED  0x04
#define MI_TEXT_FLAG_DECIMAL 0x08

class MIText: public MIValue {
protected:
    char* buffer;
    int bufsize;

    virtual bool commit() { return true; };
public:
    uint8_t flags = MI_TEXT_FLAG_TEXT | MI_TEXT_FLAG_NUMBER | MI_TEXT_FLAG_SIGNED | MI_TEXT_FLAG_DECIMAL;
    MIText(GUI* gui, const char* label, void* value, int size): MIValue(gui, label, value)  { 
        bufsize = size;
        buffer = new char[size];
    }

    MIText(GUI* gui, const char* label, void* value): MIValue(gui, label, value)  { }

    void onInput(char c) override {
        if (c == '\n') {
            if (editing) commit();
            editing = !editing;
            return;
        }

        if (!editing) return;
        int pos = strlen(buffer);

        if (flags & MI_TEXT_FLAG_TEXT) {
            if (!(c >= 32 && c <= 126)) c = 0;
        } else if (flags & MI_TEXT_FLAG_NUMBER) {
            if (c == '-') {
                if (!(flags & MI_TEXT_FLAG_SIGNED)) c = 0;
                else if (pos != 0) c = 0;
            } else if (c == '.') {
                if (!(flags & MI_TEXT_FLAG_DECIMAL)) c = 0;
                if (pos < 1) c = 0;
                else {
                    for (int i = 0; i < pos; i++) { // only one dot
                        if (buffer[i] == '.') {
                            c = 0;
                            break;
                        }
                    }
                }
            } else if (!(c >= '0' && c <= '9')) {
                c = 0;
            }
        }

        if (c != 0 && pos < bufsize) {
            buffer[pos++] = c;
            buffer[pos] = 0;
            invalid = !commit();
            invalidate();
            return;
        }
    }

    String getValueString() override {
        return String(buffer);
    }
};

class MIFloat: public MIText {
    bool dbl = false;
public:
    MIFloat(GUI* gui, const char* label, float* value, uint8_t dec = 3): MIText(gui, label, value, 16)  {
        char fmt[8] = "%f";
        if (dec > 0) {
            sprintf(fmt, "%%.%uf", dec);
        }
        flags = MI_TEXT_FLAG_NUMBER | MI_TEXT_FLAG_DECIMAL;
        sprintf(buffer, fmt, *value);
    };

     MIFloat(GUI* gui, const char* label, double* value, uint8_t dec = 3): MIText(gui, label, value, 16)  {
        dbl = true;
        char fmt[8] = "%f";
        if (dec > 0) {
            sprintf(fmt, "%%.%uf", dec);
        }
        flags = MI_TEXT_FLAG_NUMBER | MI_TEXT_FLAG_DECIMAL;
        sprintf(buffer, fmt, *value);
    };

protected:
    bool commit() override {
        char* end;
        float val = strtof(buffer, &end);
        if (val != NAN) {
            if (dbl)
                *(double*) value = val;
            else
                *(float*) value = val;
            return true;
        }
        return false;
    }
};

class MIString: public MIText {
public:
    MIString(GUI* gui, const char* label, char* value, int size): MIText(gui, label, value)  {
        buffer = value;
        bufsize = size;
    };
};

class MIInteger: public MIText {
private:
    enum Type {
        TYPE_U8,
        TYPE_S8,
        TYPE_U32,
        TYPE_S32,
    };

    Type type;
public:
    MIInteger(GUI* gui, const char* label, uint8_t* value): MIText(gui, label, value, 4)  {
        flags = MI_TEXT_FLAG_NUMBER;
        type = TYPE_U8;
        sprintf(buffer, "%u", *value);
    };

    MIInteger(GUI* gui, const char* label, int8_t* value): MIText(gui, label, value, 5)  {
        flags = MI_TEXT_FLAG_NUMBER | MI_TEXT_FLAG_SIGNED;
        type = TYPE_S8;
        sprintf(buffer, "%d", *value);
    };

        MIInteger(GUI* gui, const char* label, uint32_t* value): MIText(gui, label, value, 11)  {
        flags = MI_TEXT_FLAG_NUMBER;
        type = TYPE_U32;
        sprintf(buffer, "%u", *value);
    };

    MIInteger(GUI* gui, const char* label, int32_t* value): MIText(gui, label, value, 12)  {
        flags = MI_TEXT_FLAG_NUMBER | MI_TEXT_FLAG_SIGNED;
        type = TYPE_S32;
        sprintf(buffer, "%d", *value);
    };

protected:
    bool commit() override {
        if (type == TYPE_U8) {
            uint8_t v;
            if (sscanf(buffer, "%u", &v) == 1) {
                *(uint8_t*) value = v;
                return true;
            }
        } else if (type == TYPE_S8) {
            int8_t v;
            if (sscanf(buffer, "%d", &v) == 1) {
                *(int8_t*) value = v;
                return true;
            }
        } else if (type == TYPE_U32) {
            uint32_t v;
            if (sscanf(buffer, "%u", &v) == 1) {
                *(uint32_t*) value = v;
                return true;
            }
        } else if (type == TYPE_S32) {
            int32_t v;
            if (sscanf(buffer, "%d", &v) == 1) {
                *(int32_t*) value = v;
                return true;
            }
        }
        return false;
    }
};

class MIPage: public MILabel {
    Page* next = nullptr;
public:
    MIPage(GUI* gui, const char* label, Page* page): MILabel(gui, label) {
        next = page;
        ro = true;
    }

    void onInput(char c) override {
        if (c == 0x0A) {
            // LF
            if (next) {
                gui->stack(next);
            }
        }
    };
};

class MIAction: public MILabel {
private:
    bool(*action)(MIAction*);
public:
    MIAction(GUI* gui, const char* label, bool(*func_ptr)(MIAction*)): MILabel(gui, label) {
        ro = true;
        action = func_ptr;
    }

    void onInput(char c) override {
        if (c == 0x0A) {
            action(this);
        }
    };
};

class Page: public GUIElement {
public:
    Page(GUI* gui): GUIElement(gui) { }
    virtual void draw();
    virtual void onInput(char c);
    virtual bool onPop() { return true; };
};

class Menu: public Page {
    // TODO: Paginate
    char pageno[8] = "1/1";
    MIString* title;
    std::vector<MI*> menuitems;
    int selected = 0;
    bool fill = true;

    bool onPop() override {
        fill = true;
        return true;
    }

public:
    bool wrap = false;
    Menu(GUI* gui, const char* label): Page(gui) {
        title = new MIString(gui, label, pageno, 8);
    }

    void add(MI* mi) {
        if (!mi) return;
        menuitems.push_back(mi);
    }

    void draw() override;
    void onInput(char c) override;

    virtual void invalidate() override { 
        GUIElement::invalidate();
        title->invalidate();
        for (auto &mi: menuitems) {
            mi->invalidate();
        }
    }
};

class Boot: public Page {
    public:
    Boot(GUI* gui): Page(gui) {}
    void draw() override;
};

class Keeb: public Page {
    public:
    Keeb(GUI* gui): Page(gui) {}
    void draw() override {
        uint16_t h = gui->tft->height();
        uint16_t w = gui->tft->width();
        uint16_t ystep = h / 8;
        uint16_t xstep = w / 12;

        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 12; col++) {
                uint16_t x = (xstep * col);
                uint16_t y = h - 1 - (ystep * row);
                
                gui->tft->drawRect(x, y, xstep - 1, ystep - 1, TFT_BLACK);
            }
        }
    }
};

class Channel: public Page {
    // TODO: feed messages from mesh
    char buffer[128] = "";
    size_t bufsize = 128;
    std::vector<message>* messages;
    std::vector<message>* outmessages;
    int lastsize = -1;

    bool onPop() override {
        lastsize = -1;
        return true;
    }
public:
    Channel(GUI* gui, std::vector<message>* in, std::vector<message>* out): Page(gui) { 
        this->messages = in;
        this->outmessages = out;
    }
    int stripes[4] = {
        0xFFFF,
        0xe73c,
        0xdf7f,
        0xBF1F
    };

    void draw() override {
        // Draw from bottom
        if (redraw) {
            int16_t y = 0;
            drawInput(y);

            long start = millis();
            if (messages->size() != lastsize) {
                // TODO: size may be truncated 
                gui->tft->setTextSize(1*MI_SCALE);
                gui->tft->setTextWrap(true);
                lastsize = messages->size();
                uint16_t ih = gui->tft->fontHeight();
                for (int i = lastsize - 1; i >= 0; i--) {
                    message m = messages->at(i);
                    int16_t textw = gui->tft->textWidth(m.msg);
                    // textw += gui->tft->textWidth("00:00 ");
                    int16_t lines = (textw / gui->tft->width()) + 1;
                    int16_t texth = (lines * MI_FONT_HEIGHT);

                    y -= texth + MI_FONT_PADDING;
                    uint16_t color = i % 2;
                    if (m.me) color += 2;

                    TFT_eSprite row = TFT_eSprite(gui->tft);
                    row.createSprite(gui->tft->width(), texth + MI_FONT_PADDING);
                    row.setFreeFont(MI_FREE_FONT);
                    row.setTextSize(1*MI_SCALE);
                    row.setTextWrap(true);
                    row.setTextColor(TFT_BLACK);
                    row.fillRect(0, 0, gui->tft->width(), texth + MI_FONT_PADDING, stripes[color]);
                    row.setCursor(MI_FONT_PADDING, 0);
                    row.print(m.msg);
                    row.pushSprite(0, y);

                    // gui->tft->fillRect(0, y, gui->tft->width(), texth + MI_FONT_PADDING, stripes[color]);
                    // gui->tft->setCursor(MI_FONT_PADDING, y);
                    // // gui->tft->printf("%02u:%02u ", m.hh, m.mm);
                    // gui->tft->print(m.msg);

                    if (y < 0) break;
                }
                // fill top
                if (y > 0) {
                    gui->tft->fillRect(0, 0, gui->tft->width(), y, MI_COLOR_BKG);
                }
            }
            Serial.printf("Page render took %u ms\n", millis() - start);
        }
        onDraw();
    }

    void drawInput(int16_t &y) {
        uint16_t h = gui->tft->height();
        uint16_t w = gui->tft->width();


        uint16_t ih = MI_FONT_HEIGHT + MI_FONT_PADDING + MI_FONT_PADDING;
        uint16_t tw = gui->tft->textWidth(buffer);
        int16_t  x = MI_FONT_PADDING;

        y = h - ih;

        TFT_eSprite row = TFT_eSprite(gui->tft);
        row.createSprite(gui->tft->width(), ih);
        row.setFreeFont(MI_FREE_FONT);
        row.setTextSize(1*MI_SCALE);
        row.setTextWrap(false);
        row.setTextColor(TFT_BLACK);
        row.drawLine(0, 0, w, y, TFT_ORANGE);
        row.fillRect(0, 1, w, ih, TFT_WHITE);
        row.setCursor(x, 0 + MI_FONT_PADDING);
        row.print(buffer);
        row.pushSprite(0, y);
    }

    void onInput(char c) override {
        size_t pos = strlen(buffer);
        if (c == 0x0A) {
            // send
            if (pos < 1) return;
            this->outmessages->push_back(message(buffer, 12, this->messages->size(), true));
            buffer[0] = 0;
            invalidate();
        } else if (c == 0x08) {
            // del
            size_t pos = strlen(buffer);
            if (pos > 0) {
                buffer[(pos--)-1] = 0;
                invalidate();
            }
        } else if (c >= 32 && c <= 0x95) { // extended for latvian chars
            size_t pos = strlen(buffer);
            if (pos < bufsize) buffer[pos] = c; buffer[pos+1] = 0;
            invalidate();
        }
    };
};

#endif