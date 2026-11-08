#include "json.h"
#include <stdlib.h>
#include <string.h>

Json::~Json() { free(tok); }

int Json::scan(JTok* out, int cap, size_t len) const {
  int count = 0;
  int stack[32];
  int depth = 0;
  for (size_t p = 0; p < len; p++) {
    char c = src[p];
    if (c == '{' || c == '[') {
      if (depth && out && out[stack[depth - 1]].type == J_ARR) out[stack[depth - 1]].size++;
      if (out) {
        if (count >= cap) return -1;
        out[count] = {c == '{' ? J_OBJ : J_ARR, (uint32_t)p, 0, 0};
      }
      if (depth == 32) return -1;
      stack[depth++] = count++;
    } else if (c == '}' || c == ']') {
      if (!depth) return -1;
      depth--;
      if (out) out[stack[depth]].end = p + 1;
    } else if (c == '"') {
      size_t s = ++p;
      while (p < len && src[p] != '"') {
        if (src[p] == '\\') p++;
        p++;
      }
      if (p >= len) return -1;
      bool isKey = false;
      if (depth && out && out[stack[depth - 1]].type == J_OBJ) {
        size_t q = p + 1;
        while (q < len && (src[q] == ' ' || src[q] == '\n' || src[q] == '\r' || src[q] == '\t')) q++;
        isKey = q < len && src[q] == ':';
      }
      if (out) {
        if (count >= cap) return -1;
        out[count] = {J_STR, (uint32_t)s, (uint32_t)p, 0};
        if (depth && (isKey || out[stack[depth - 1]].type == J_ARR)) out[stack[depth - 1]].size++;
        if (isKey) out[count].size = 1;
      }
      count++;
    } else if (c == '-' || (c >= '0' && c <= '9') || c == 't' || c == 'f' || c == 'n') {
      size_t s = p;
      while (p < len && !strchr(",]} \n\r\t", src[p])) p++;
      if (out) {
        if (count >= cap) return -1;
        out[count] = {J_PRIM, (uint32_t)s, (uint32_t)p, 0};
        if (depth && out[stack[depth - 1]].type == J_ARR) out[stack[depth - 1]].size++;
      }
      count++;
      p--;
    }
  }
  return depth ? -1 : count;
}

bool Json::parse(const char* s, size_t len) {
  src = s;
  free(tok);
  tok = nullptr;
  n = 0;
  int need = scan(nullptr, 0, len);
  if (need <= 0) return false;
  tok = (JTok*)malloc(sizeof(JTok) * need);
  if (!tok) return false;
  n = scan(tok, need, len);
  return n > 0;
}

int Json::skip(int t) const {
  int end = tok[t].end;
  if (tok[t].type == J_STR && tok[t].size) {
    t++;
    return skip(t);
  }
  if (tok[t].type == J_OBJ || tok[t].type == J_ARR) {
    t++;
    while (t < n && tok[t].start < (uint32_t)end) t++;
    return t;
  }
  return t + 1;
}

int Json::get(int obj, const char* key) const {
  if (obj < 0 || tok[obj].type != J_OBJ) return -1;
  size_t kl = strlen(key);
  int t = obj + 1;
  for (int i = 0; i < tok[obj].size && t < n; i++) {
    const JTok& k = tok[t];
    if (k.end - k.start == kl && !memcmp(src + k.start, key, kl)) return t + 1;
    t = skip(t);
  }
  return -1;
}

int Json::at(int arr, int i) const {
  if (arr < 0 || tok[arr].type != J_ARR || i >= tok[arr].size) return -1;
  int t = arr + 1;
  while (i-- > 0) t = skip(t);
  return t;
}

long Json::num(int t, long def) const {
  if (t < 0 || tok[t].type != J_PRIM) return def;
  char c = src[tok[t].start];
  if (c == 'n') return def;
  if (c == 't') return 1;
  if (c == 'f') return 0;
  return (long)strtod(src + tok[t].start, nullptr);
}

double Json::dbl(int t, double def) const {
  if (t < 0 || tok[t].type != J_PRIM || src[tok[t].start] == 'n') return def;
  return strtod(src + tok[t].start, nullptr);
}

bool Json::flag(int t, bool def) const {
  if (t < 0 || tok[t].type != J_PRIM) return def;
  char c = src[tok[t].start];
  if (c == 't') return true;
  if (c == 'f' || c == 'n') return c == 'n' ? def : false;
  return strtod(src + tok[t].start, nullptr) != 0;
}

static size_t putUtf8(char* o, size_t i, size_t cap, uint32_t cp) {
  char b[4];
  int n;
  if (cp < 0x80) { b[0] = cp; n = 1; }
  else if (cp < 0x800) { b[0] = 0xC0 | cp >> 6; b[1] = 0x80 | (cp & 63); n = 2; }
  else { b[0] = 0xE0 | cp >> 12; b[1] = 0x80 | ((cp >> 6) & 63); b[2] = 0x80 | (cp & 63); n = 3; }
  if (i + n >= cap) return i;
  memcpy(o + i, b, n);
  return i + n;
}

size_t Json::str(int t, char* out, size_t cap, const char* def) const {
  if (!cap) return 0;
  if (t < 0 || tok[t].type != J_STR) {
    strncpy(out, def, cap - 1);
    out[cap - 1] = 0;
    return strlen(out);
  }
  size_t i = 0;
  for (uint32_t p = tok[t].start; p < tok[t].end && i + 1 < cap; p++) {
    char c = src[p];
    if (c != '\\') {
      uint8_t u = c;
      size_t len = u < 0x80 ? 1 : u >= 0xF0 ? 4 : u >= 0xE0 ? 3 : u >= 0xC0 ? 2 : 1;
      if (i + len >= cap) break;
      memcpy(out + i, src + p, len);
      i += len;
      p += len - 1;
      continue;
    }
    c = src[++p];
    switch (c) {
      case 'n': out[i++] = '\n'; break;
      case 't': out[i++] = ' '; break;
      case 'r': break;
      case 'u': {
        char h[5] = {0};
        memcpy(h, src + p + 1, 4);
        i = putUtf8(out, i, cap, strtoul(h, nullptr, 16));
        p += 4;
        break;
      }
      default: out[i++] = c;
    }
  }
  out[i] = 0;
  return i;
}

uint32_t Json::hex(int t, uint32_t def) const {
  char b[12];
  if (!str(t, b, sizeof b)) return def;
  return strtoul(b[0] == '#' ? b + 1 : b, nullptr, 16);
}
