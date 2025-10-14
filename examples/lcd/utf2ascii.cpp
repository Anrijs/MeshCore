#include "utf2ascii.h"

String ascii2utf(String s) {
  String r = "";
  int len = s.length();
  for (int i=0;i<len;i++) {
    char c = s.charAt(i);
    if (c <= 127) {
      r += c;
    } else {
      // special symbols / latv
      if (c == '\x80') r+= "\xC4\x80"; // 'A'
      else if (c == '\x81') r+= "\xC4\x8C"; // 'C'
      else if (c == '\x82') r+= "\xC4\x92"; // 'E'
      else if (c == '\x83') r+= "\xC4\xA2"; // 'Ģ'
      else if (c == '\x84') r+= "\xC4\xAA"; // 'I'
      else if (c == '\x85') r+= "\xC4\xB6"; // 'K'
      else if (c == '\x86') r+= "\xC4\xBB"; // 'L'
      else if (c == '\x87') r+= "\xC5\x85"; // 'N'
      else if (c == '\x88') r+= "\xC5\xA0"; // 'S'
      else if (c == '\x89') r+= "\xC5\xAA"; // 'U'
      else if (c == '\x8A') r+= "\xC5\xBD"; // 'Z'
      else if (c == '\x8B') r+= "\xC4\x81"; // 'a'
      else if (c == '\x8C') r+= "\xC4\x8D"; // 'c'
      else if (c == '\x8D') r+= "\xC4\x93"; // 'e'
      else if (c == '\x8E') r+= "\xC4\xA3"; // 'ģ'
      else if (c == '\x8F') r+= "\xC4\xAB"; // 'i'
      else if (c == '\x90') r+= "\xC4\xB7"; // 'k'
      else if (c == '\x91') r+= "\xC4\xBC"; // 'l'
      else if (c == '\x92') r+= "\xC5\x86"; // 'n'
      else if (c == '\x93') r+= "\xC5\xA1"; // 's'
      else if (c == '\x94') r+= "\xC5\xAB"; // 'u'
      else if (c == '\x95') r+= "\xC5\xBE"; // 'z'
      else if (c == '\x96') r+= "\xEF\xBF\xBD"; // <?>'
    }
  }
  return r;
}

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

      // Works only with custom Latv font
      if (utf8code == 0xC4800000) r+= '\x80'; // 'A'
      else if (utf8code == 0xC48C0000) r+= '\x81'; // 'C'
      else if (utf8code == 0xC4920000) r+= '\x82'; // 'E'
      else if (utf8code == 0xC4A20000) r+= '\x83'; // 'Ģ'
      else if (utf8code == 0xC4AA0000) r+= '\x84'; // 'I'
      else if (utf8code == 0xC4B60000) r+= '\x85'; // 'K'
      else if (utf8code == 0xC4BB0000) r+= '\x86'; // 'L'
      else if (utf8code == 0xC5850000) r+= '\x87'; // 'N'
      else if (utf8code == 0xC5A00000) r+= '\x88'; // 'S'
      else if (utf8code == 0xC5AA0000) r+= '\x89'; // 'U'
      else if (utf8code == 0xC5BD0000) r+= '\x8A'; // 'Z'
      else if (utf8code == 0xC4810000) r+= '\x8B'; // 'a'
      else if (utf8code == 0xC48D0000) r+= '\x8C'; // 'c'
      else if (utf8code == 0xC4930000) r+= '\x8D'; // 'e'
      else if (utf8code == 0xC4A30000) r+= '\x8E'; // 'ģ'
      else if (utf8code == 0xC4AB0000) r+= '\x8F'; // 'i'
      else if (utf8code == 0xC4B70000) r+= '\x90'; // 'k'
      else if (utf8code == 0xC4BC0000) r+= '\x91'; // 'l'
      else if (utf8code == 0xC5860000) r+= '\x92'; // 'n'
      else if (utf8code == 0xC5A10000) r+= '\x93'; // 's'
      else if (utf8code == 0xC5AB0000) r+= '\x94'; // 'u'
      else if (utf8code == 0xC5BE0000) r+= '\x95'; // 'z'
      else if (utf8code == 0xEFBFBD00) r+= '\x96'; // '<q>'

      // TODO: add custom emoji font????
      // else if (utf8code == 0xF09F9880) r+= ":)";
      // else if (utf8code == 0xF09F9881) r+= ":D";
      // else if (utf8code == 0xF09F9882) r+= ":'D";
      // else if (utf8code == 0xF09F9883) r+= ":)";
      // else if (utf8code == 0xF09F9884) r+= ":D";
      // else if (utf8code == 0xF09F9885) r+= ":'D";
      // else if (utf8code == 0xF09F988E) r+= "8)";

      else { 
        Serial.printf("-- Unknown code: %08X\n", utf8code);
        r += '\x96';
      }
    }
  }
  return r;
}
