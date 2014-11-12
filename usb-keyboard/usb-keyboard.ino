#define HWSERIAL Serial1
#include "utils.h"

#define KBD_BUFFSZ 200
#define PREFIX 17 // CTRL-Q
#define MYDEBUG

char inChar;
char peekChar;
char kbd_buff[KBD_BUFFSZ];
int kbd_idx = 0;
int crs_idx = 0;

int in_mode = 1;
enum mode {INVALID, COMMAND, INTERACTIVE, DEBUG, TEST};
const char * mode_strings[] = {"invalid", "command", "interactive", "debug", "test"};
char * selectMode = "Select Inputmode: [1] Command - [2] Interactive - [3] Debug";

void setup() {
  HWSERIAL.begin(115200);
  delay(1000);
  SerialClear();
  SerialPrintfln("Switching to %s mode", mode_strings[in_mode]);
}

void loop() {
  if (HWSERIAL.available() > 0) {
    inChar = HWSERIAL.read();

    if (inChar == PREFIX) {
      SerialPrintf("%s", selectMode);
      do {
        while (HWSERIAL.available() == 0) {
          delay(100);
        }
        peekChar = HWSERIAL.peek();
        if (peekChar == PREFIX) {
          inChar = HWSERIAL.read();
          Keyboard.print(inChar);
          SerialDeleteChars(strlen(selectMode));
          return;
        } else if (peekChar > '0' && peekChar < ('0'+sizeof(mode_strings)/sizeof(char*))) {
          inChar = HWSERIAL.read();
          in_mode = inChar - '0';
          SerialClear();
          SerialPrintfln("Switching to %s mode", mode_strings[in_mode]);
          return;
        } else if (peekChar == 27) {
          inChar = HWSERIAL.read();
          SerialDeleteChars(strlen(selectMode));
          return;
        } else {
          inChar = HWSERIAL.read();
          SerialDeleteChars(strlen(selectMode));
          SerialPrintf("%s", selectMode);
        }
      } while (peekChar != 27 || peekChar != PREFIX || (peekChar >= 49 && peekChar <= 51));
    }

    switch(in_mode) {
      case COMMAND:
        command_mode(inChar);
        break;

      case INTERACTIVE:
        interactive_mode(inChar);
        break;

      case DEBUG:
        SerialPrintfln("Recv -> Character: %c - ASCII-Code: %3i - USB-Scancode: %3i", inChar, inChar, unicode_to_keycode(inChar));
        //SerialPrintfln("Recv -> ASCII-Code:: %3i", inChar);
        break;

      case 4: //VERBOSE DEBUG MODE
        SerialPrintfln("Recv -> Character: %c - ASCII-Code: %3i", inChar, inChar);

        char keycode_b[33];
        char key_b[33];
        char mod_b[33];

        KEYCODE_TYPE keycode = unicode_to_keycode(inChar);
        itoa(keycode, keycode_b, 2);

        uint8_t key = keycode_to_key(keycode);
        itoa(key, key_b, 2);

        uint8_t mod = keycode_to_modifier(keycode);
        itoa(mod, mod_b, 2);

        SerialPrintfln("keycode: %3i = %08s | key: %3i = %08s | mod: %3i = %08s", keycode, keycode_b, key, key_b, mod, mod_b);
        break;
    }
  }
}

// Util functions
uint16_t escape_sequence_to_keycode(char key) {
  char in_ascii = key;
  uint16_t keycode = 0;

  if (in_ascii == 91) {
    in_ascii = HWSERIAL.peek();
    if (in_ascii >= 65 && in_ascii <= 68) {
      HWSERIAL.read();
      if (in_ascii == 65)
        keycode = KEY_UP;
      if (in_ascii == 66)
        keycode = KEY_DOWN;
      if (in_ascii == 67)
        keycode = KEY_RIGHT;
      if (in_ascii == 68)
        keycode = KEY_LEFT;
    } else if (in_ascii == 51) {
      HWSERIAL.read();
      in_ascii = HWSERIAL.peek();
      if (in_ascii == 126) {
        HWSERIAL.read();
        keycode = KEY_DELETE;
      }
    }
  }
  return keycode;
}

uint16_t special_char_to_keycode(char key) {
  char in_ascii = key;
  uint16_t keycode = 0;

  switch(in_ascii) {
    case 10:  //LF
    case 13:  //CR
      keycode = KEY_ENTER;
      break;
    case 8:   //BS
    case 127: //DEL
      keycode = KEY_BACKSPACE;
      break;
    case 9:   //HT
      keycode = KEY_TAB;
      break;
    case 27:
      in_ascii = HWSERIAL.peek();
      if(in_ascii == 255) {
        keycode = KEY_ESC;
      } else {
        HWSERIAL.read();
        keycode = escape_sequence_to_keycode(in_ascii);
      }
      break;
  }
  return keycode;
}

void send_key(uint8_t key, uint8_t mod=0) {
  Keyboard.set_key1(key);
  if (mod) Keyboard.set_modifier(mod);
  Keyboard.send_now();
  Keyboard.set_modifier(0);
  Keyboard.set_key1(0);
  Keyboard.send_now();
}


// Interactive mode functions
void interactive_mode(char key) {
  char in_ascii = key;
  KEYCODE_TYPE keycode = special_char_to_keycode(in_ascii);

  if (keycode) {
    send_key(keycode);
  } else if (in_ascii <= 26) {
    Keyboard.set_modifier(MODIFIERKEY_CTRL);
    keycode = in_ascii+3;
    send_key(keycode);
  } else {
    Keyboard.write(in_ascii);
    HWSERIAL.write(in_ascii);
  }
}

// Command mode functions
void command_mode(char key) {
  char in_ascii = key;
  uint16_t keycode = special_char_to_keycode(in_ascii);

  if(keycode) {
    switch(keycode) {
      case KEY_ENTER:
        SerialPrintfln("");
        kbd_buff[kbd_idx] = '\0';
        c_parse(kbd_buff);
        crs_idx = kbd_idx;
        kbd_idx = 0;
        break;
      case KEY_BACKSPACE:
        SerialDeleteChars(1);
        if (kbd_idx>0) kbd_idx--;
        break;
      case KEY_LEFT:
        if (crs_idx>0) {
          SerialAnsiEsc("1D");
          crs_idx--;
        }
        break;
      case KEY_RIGHT:
        if (crs_idx<kbd_idx) {
          SerialAnsiEsc("1C");
          crs_idx++;
        }
        break;
      case KEY_UP:
        while (crs_idx>kbd_idx)
          HWSERIAL.write(kbd_buff[kbd_idx++]);
        break;
    }
  } else if (in_ascii == 3) {
    SerialClearLine();
    crs_idx = kbd_idx;
    kbd_idx = 0;
  } else if (kbd_idx >= KBD_BUFFSZ-1) {
    command_mode('\n');
  } else if (in_ascii <= 26) {
  } else {
    HWSERIAL.write(in_ascii);
    if (crs_idx>kbd_idx) crs_idx = kbd_idx;
    kbd_buff[crs_idx++] = in_ascii;
    if (kbd_idx<crs_idx) kbd_idx++;
  }
}

void c_parse(char * str) {
  int state = 0;
  char * pch;

  pch = strtok(str," ");
#ifdef MYDEBUG
  SerialPrintfln("\t%-10s -> %x", pch, str2int(pch));
#endif
  switch (str2int(pch)) {
    case str2int("SendRaw"):
      // Send the rest of the line literally
      pch = strtok (NULL,"");
      if (pch != NULL)
        c_sendraw(pch);
      break;

    case str2int("Send"):
      // Send the rest of the line (and parse special characters)
      pch = strtok (NULL,"");
      if (pch != NULL)
        c_send(pch);
      break;

    case str2int("UnicodeLinux"):
    case str2int("UCL"):
      // Send a single unicode character (on Linux)
      pch = strtok (NULL,"");
      if (pch != NULL)
        c_unicode(pch, true);
      break;

    case str2int("UnicodeWindows"):
    case str2int("UCW"):
      // Send a single unicode character (on Windows)
      pch = strtok (NULL,"");
      if (pch != NULL)
        c_unicode(pch, false);
      break;

    case str2int("Sleep"):
      // Sleep a certain amount of time in ms
      pch = strtok (NULL,"");
      if (pch != NULL)
        c_sleep(pch);
      break;

    case str2int("Enter"):
      // Send the Enter Key
      //TODO
      break;

    case str2int("Help"):
      // Display a informative help message
      //TODO
      break;

    default:
      // Handle unknown command
      //TODO
      break;
  }
}

void c_sendraw(char * pch) {
  char * c = pch;

  while (*c != NULL) {
    KEYCODE_TYPE keycode = unicode_to_keycode(*c);
    uint8_t key = keycode_to_key(keycode);
    uint8_t mod = keycode_to_modifier(keycode);
#ifdef MYDEBUG
    SerialPrintfln("Writing %c via USB. Keycode: %3i", *c, key);
#endif
    send_key(key, mod);
    c++;
  }
}

void c_send(char * pch) {
  char * c = pch;
  int modifier = 0;

  while (*c != NULL) {
    switch (*c) {
      case '!':
        // ALT
        modifier |= MODIFIERKEY_ALT;
        break;
      case '+':
        // SHIFT
        modifier |= MODIFIERKEY_SHIFT;
        break;
      case '^':
        // CTRL
        modifier |= MODIFIERKEY_CTRL;
        break;
      case '#':
        // WIN
        modifier |= MODIFIERKEY_GUI;
        break;
      default:
        KEYCODE_TYPE keycode = unicode_to_keycode(*c);
        uint8_t key = keycode_to_key(keycode);
        uint8_t mod = keycode_to_modifier(keycode) | modifier;
#ifdef MYDEBUG
        SerialPrintfln("Writing %c via USB. Keycode: %3i", *c, key);
#endif
        send_key(key, mod);
        break;
    }
    c++;
  }
}

void c_unicode(char * pch, bool linux) {
  //XXX
}

void c_sleep(char * pch) {
  int time = atoi(pch);
#ifdef MYDEBUG
  SerialPrintfln("\tSleep %i ms", time);
#endif
  //XXX
}
