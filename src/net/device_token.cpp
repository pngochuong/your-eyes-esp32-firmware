#include "device_token.h"

#include <Preferences.h>

static const char *NS  = "visioncare";
static const char *KEY = "token";

bool tokenLoad(char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  Preferences p;
  if (!p.begin(NS, true)) return false;       // chua tung ghi -> namespace chua co
  size_t n = p.getString(KEY, out, cap);
  p.end();
  return n > 0 && out[0] != 0;
}

bool tokenSave(const char *token) {
  size_t len = token ? strlen(token) : 0;
  if (len > DEVICE_TOKEN_MAX) return false;
  Preferences p;
  if (!p.begin(NS, false)) return false;
  bool ok = len ? p.putString(KEY, token) == len : p.remove(KEY);
  p.end();
  return ok;
}
