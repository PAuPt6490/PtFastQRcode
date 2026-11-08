"""Turn a string into PC set-1 scancodes for VBoxManage keyboardputscancode."""
import sys

BASE = {
    '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05, '5': 0x06, '6': 0x07, '7': 0x08,
    '8': 0x09, '9': 0x0A, '0': 0x0B, '-': 0x0C, '=': 0x0D,
    'q': 0x10, 'w': 0x11, 'e': 0x12, 'r': 0x13, 't': 0x14, 'y': 0x15, 'u': 0x16,
    'i': 0x17, 'o': 0x18, 'p': 0x19, '[': 0x1A, ']': 0x1B,
    'a': 0x1E, 's': 0x1F, 'd': 0x20, 'f': 0x21, 'g': 0x22, 'h': 0x23, 'j': 0x24,
    'k': 0x25, 'l': 0x26, ';': 0x27, "'": 0x28, '`': 0x29, '\\': 0x2B,
    'z': 0x2C, 'x': 0x2D, 'c': 0x2E, 'v': 0x2F, 'b': 0x30, 'n': 0x31, 'm': 0x32,
    ',': 0x33, '.': 0x34, '/': 0x35, ' ': 0x39,
}
SHIFTED = {
    ':': ';', '_': '-', '\\': '\\', '"': "'", '(': '9', ')': '0', '*': '8',
    '+': '=', '{': '[', '}': ']', '?': '/', '<': ',', '>': '.', '!': '1',
    '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '|': '\\',
    '~': '`',
}
SPECIAL = {
    'enter': [0x1C], 'tab': [0x0F], 'esc': [0x01], 'backspace': [0x0E],
    'space': [0x39], 'win+r': [0xE0, 0x5B, 0x13, 0x93, 0xE0, 0xDB],
    'win': [0xE0, 0x5B, 0xE0, 0xDB], 'ctrl+esc': [0x1D, 0x9D, 0x01, 0x81],
    'f5': [0x3F], 'delete': [0xE0, 0x53, 0xE0, 0xD3],
}


def codes(text):
    out = []
    for ch in text:
        if ch in BASE:
            code = BASE[ch]
            out += [code, code | 0x80]
        elif ch in SHIFTED:
            code = BASE[SHIFTED[ch]]
            out += [0x2A, code, code | 0x80, 0xAA]
        elif ch.isupper():
            code = BASE[ch.lower()]
            out += [0x2A, code, code | 0x80, 0xAA]
        else:
            raise SystemExit('no scancode for %r' % ch)
    return out


if __name__ == '__main__':
    what = sys.argv[1]
    if what == '--special':
        print(' '.join('%02x' % c for c in SPECIAL[sys.argv[2]]))
    else:
        print(' '.join('%02x' % c for c in codes(what)))
