#include "pge/settings.h"
#include <string.h>
#include "pge/config.h"

namespace pge {

struct Settings::Entry {
  char key[PGE_CONFIG_KEY_BYTES];
  char value[PGE_CONFIG_VALUE_BYTES];
};

namespace {

bool isBlank(char c) { return c == ' ' || c == '\t'; }

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

Error Settings::init(Arena& a, uint16_t max_keys) {
  if (max_keys == 0) max_keys = PGE_CONFIG_MAX_KEYS;
  Entry* t = a.allocArray<Entry>(max_keys);
  if (!t) return OutOfMemory;
  tab_ = t;
  cap_ = max_keys;
  count_ = skipped_ = 0;
  return Ok;
}

Settings::Entry* Settings::find(const char* key, uint32_t n) const {
  // shortcut: linear scan, 32 keys max
  for (uint16_t i = 0; i < count_; i++)
    if (strlen(tab_[i].key) == n && memcmp(tab_[i].key, key, n) == 0) return &tab_[i];
  return nullptr;
}

void Settings::parse(const char* text, uint32_t len) {
  uint32_t pos = 0;
  while (pos < len) {
    uint32_t end = pos;
    while (end < len && text[end] != '\n') end++;
    uint32_t next = end < len ? end + 1 : end;
    if (end > pos && text[end - 1] == '\r') end--;
    const char* s = text + pos;
    uint32_t n = end - pos;
    pos = next;

    while (n && isBlank(*s)) { s++; n--; }
    if (n == 0 || *s == '#') continue;  // blank line or comment: not counted

    uint32_t eq = 0;
    while (eq < n && s[eq] != '=') eq++;
    if (eq == n) { if (skipped_ != 0xFFFF) skipped_++; continue; }  // no '='

    uint32_t kl = eq;
    while (kl && isBlank(s[kl - 1])) kl--;
    const char* v = s + eq + 1;
    uint32_t vl = n - eq - 1;
    while (vl && isBlank(*v)) { v++; vl--; }
    while (vl && isBlank(v[vl - 1])) vl--;

    const bool bad = kl == 0 || kl >= PGE_CONFIG_KEY_BYTES || vl >= PGE_CONFIG_VALUE_BYTES;
    Entry* e = bad ? nullptr : find(s, kl);
    if (bad || (!e && count_ == cap_)) {
      if (skipped_ != 0xFFFF) skipped_++;  // empty/over-long key, over-long value or new key in a full table
      continue;
    }
    if (!e) {
      e = &tab_[count_++];
      memcpy(e->key, s, kl);
      e->key[kl] = 0;
    }
    memcpy(e->value, v, vl);
    e->value[vl] = 0;
  }
}

Error Settings::load(Storage& st, const char* path, void* scratch, uint32_t scratch_bytes) {
  uint32_t len = 0;
  const Error e = st.readFile(path, scratch, scratch_bytes, len);
  if (e != Ok) return e;
  parse(static_cast<const char*>(scratch), len);
  return Ok;
}

const char* Settings::getStr(const char* key, const char* def) const {
  if (!key) return def;
  const Entry* e = find(key, (uint32_t)strlen(key));
  return e ? e->value : def;
}

int32_t Settings::getInt(const char* key, int32_t def) const {
  const char* p = getStr(key, nullptr);
  if (!p) return def;
  bool neg = false;
  if (*p == '-' || *p == '+') neg = *p++ == '-';
  uint32_t base = 10;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
  if (*p == 0) return def;  // empty value, lone sign or "0x"
  const uint64_t limit = neg ? 0x80000000ull : 0x7FFFFFFFull;
  uint64_t acc = 0;
  for (; *p; p++) {
    const int d = hexDigit(*p);
    if (d < 0 || (uint32_t)d >= base) return def;
    acc = acc * base + (uint32_t)d;
    if (acc > limit) return def;  // int32 overflow
  }
  return neg ? (int32_t)(0 - (uint32_t)acc) : (int32_t)acc;
}

}  // namespace pge
