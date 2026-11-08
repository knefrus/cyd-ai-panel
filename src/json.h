#pragma once
#include <stddef.h>
#include <stdint.h>

enum JType : uint8_t { J_NONE, J_OBJ, J_ARR, J_STR, J_PRIM };

struct JTok {
  JType type;
  uint32_t start, end;
  uint16_t size;
};

class Json {
 public:
  bool parse(const char* s, size_t len);
  ~Json();

  int root() const { return n ? 0 : -1; }
  int get(int obj, const char* key) const;
  int at(int arr, int i) const;
  int size(int t) const { return t < 0 ? 0 : tok[t].size; }
  JType type(int t) const { return t < 0 ? J_NONE : tok[t].type; }

  long num(int t, long def = 0) const;
  double dbl(int t, double def = 0) const;
  bool flag(int t, bool def = false) const;
  size_t str(int t, char* out, size_t cap, const char* def = "") const;
  uint32_t hex(int t, uint32_t def = 0) const;

 private:
  const char* src = nullptr;
  JTok* tok = nullptr;
  int n = 0;
  int skip(int t) const;
  int scan(JTok* out, int cap, size_t len) const;
};
