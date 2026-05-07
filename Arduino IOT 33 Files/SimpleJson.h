// ====================================================================
// SimpleJson.h  –  Flat JSON parser + builder for Arduino
// Task 3: MQTT + JSON | IEM – Hochschule Ravensburg-Weingarten
//
// Supports: String, int, float, bool  (flat objects only, no arrays/nesting)
// No dynamic memory (heap) – everything lives in fixed-size arrays.
// Copy this file into BOTH sketch folders (Pico W and Nano 33 IoT).
// ====================================================================
#pragma once
#include <Arduino.h>

class SimpleJson {
public:
  // ── Limits (increase MAX_ENTRIES if you add more JSON fields) ─────
  static const int MAX_ENTRIES = 20;
  static const int MAX_KEY_LEN = 32;
  static const int MAX_VAL_LEN = 64;

  // Value type tag – needed so toCharArray knows whether to add quotes
  enum ValType { T_STRING, T_NUMBER, T_BOOL };

  struct Entry {
    char    key[MAX_KEY_LEN];
    char    val[MAX_VAL_LEN]; // stored WITHOUT surrounding quotes
    ValType type;
    bool    used;
  };

private:
  Entry entries[MAX_ENTRIES];
  int   _count;

  // ── Find index of key, -1 if not found ────────────────────────────
  int findKey(const char* k) const {
    for (int i = 0; i < _count; i++)
      if (entries[i].used && strcmp(entries[i].key, k) == 0) return i;
    return -1;
  }

  // ── Find or allocate a slot for key ───────────────────────────────
  int alloc(const char* k) {
    int i = findKey(k);
    if (i >= 0) return i;               // update existing key
    if (_count >= MAX_ENTRIES) return -1; // no space
    i = _count++;
    strncpy(entries[i].key, k, MAX_KEY_LEN - 1);
    entries[i].key[MAX_KEY_LEN - 1] = '\0';
    entries[i].used = true;
    return i;
  }

public:
  SimpleJson() { clear(); }

  // ── Reset everything ──────────────────────────────────────────────
  void clear() {
    _count = 0;
    memset(entries, 0, sizeof(entries));
  }

  // ── Check whether a key is present ───────────────────────────────
  bool hasKey(const char* k) const { return findKey(k) >= 0; }

  // ================================================================
  //  PARSE  –  fill the object from a JSON string
  //  Returns true if at least one key was extracted.
  // ================================================================
  bool parse(const char* json) {
    clear();
    const char* p = json;

    // Scan forward to opening brace
    while (*p && *p != '{') p++;
    if (!*p) return false;
    p++; // skip '{'

    while (*p && *p != '}') {
      // Skip whitespace and commas between entries
      while (*p && (*p == ' ' || *p == '\t' || *p == '\n' ||
                    *p == '\r' || *p == ',')) p++;
      if (!*p || *p == '}') break;
      if (*p != '"') { p++; continue; }  // skip unexpected chars

      // ── Read key ────────────────────────────────────────────────
      p++; // skip opening quote
      char key[MAX_KEY_LEN] = {};
      int  ki = 0;
      while (*p && *p != '"' && ki < MAX_KEY_LEN - 1) key[ki++] = *p++;
      if (*p == '"') p++; // skip closing quote

      // Skip colon (and optional spaces around it)
      while (*p && *p != ':') p++;
      if (*p == ':') p++;
      while (*p == ' ' || *p == '\t') p++;

      // ── Read value ───────────────────────────────────────────────
      char    val[MAX_VAL_LEN] = {};
      int     vi = 0;
      ValType vt  = T_NUMBER;

      if (*p == '"') {
        // String value: strip the surrounding quotes
        vt = T_STRING;
        p++; // skip opening quote
        while (*p && *p != '"' && vi < MAX_VAL_LEN - 1) val[vi++] = *p++;
        if (*p == '"') p++;
      } else {
        // Number, bool, or null – read until delimiter
        while (*p && *p != ',' && *p != '}' &&
               *p != ' ' && *p != '\t' && *p != '\n' &&
               vi < MAX_VAL_LEN - 1)
          val[vi++] = *p++;
        // Classify
        if (strcmp(val, "true") == 0 || strcmp(val, "false") == 0)
          vt = T_BOOL;
        else
          vt = T_NUMBER;
      }

      // ── Store entry ──────────────────────────────────────────────
      int idx = alloc(key);
      if (idx >= 0) {
        strncpy(entries[idx].val, val, MAX_VAL_LEN - 1);
        entries[idx].val[MAX_VAL_LEN - 1] = '\0';
        entries[idx].type = vt;
      }
    } // end while

    return _count > 0;
  }

  // ================================================================
  //  GETTERS
  // ================================================================
  String getString(const char* k) const {
    int i = findKey(k);
    return (i >= 0) ? String(entries[i].val) : String("");
  }

  int getInt(const char* k) const {
    int i = findKey(k);
    return (i >= 0) ? atoi(entries[i].val) : 0;
  }

  float getFloat(const char* k) const {
    int i = findKey(k);
    return (i >= 0) ? (float)atof(entries[i].val) : 0.0f;
  }

  bool getBool(const char* k) const {
    int i = findKey(k);
    if (i < 0) return false;
    return (strcmp(entries[i].val, "true") == 0 ||
            strcmp(entries[i].val, "1")    == 0);
  }

  // ================================================================
  //  SETTERS
  // ================================================================
  void setString(const char* k, const char* v) {
    int i = alloc(k); if (i < 0) return;
    strncpy(entries[i].val, v, MAX_VAL_LEN - 1);
    entries[i].val[MAX_VAL_LEN - 1] = '\0';
    entries[i].type = T_STRING;
  }

  void setInt(const char* k, int v) {
    int i = alloc(k); if (i < 0) return;
    snprintf(entries[i].val, MAX_VAL_LEN, "%d", v);
    entries[i].type = T_NUMBER;
  }

  void setFloat(const char* k, float v) {
    int i = alloc(k); if (i < 0) return;
    snprintf(entries[i].val, MAX_VAL_LEN, "%.2f", v);
    entries[i].type = T_NUMBER;
  }

  void setBool(const char* k, bool v) {
    int i = alloc(k); if (i < 0) return;
    strncpy(entries[i].val, v ? "true" : "false", MAX_VAL_LEN - 1);
    entries[i].type = T_BOOL;
  }

  // ================================================================
  //  SERIALIZE  –  write the object to a char buffer as JSON
  //  Strings get surrounding quotes; numbers and bools do not.
  // ================================================================
  void toCharArray(char* buf, int bufSize) const {
    int pos = 0;

    // Helper lambda: append a C-string to buf
    auto emit = [&](const char* s) {
      while (*s && pos < bufSize - 1) buf[pos++] = *s++;
    };

    emit("{");
    bool first = true;
    for (int i = 0; i < _count; i++) {
      if (!entries[i].used) continue;
      if (!first) emit(",");
      first = false;
      emit("\"");
      emit(entries[i].key);
      emit("\":");
      if (entries[i].type == T_STRING) {
        emit("\"");
        emit(entries[i].val);
        emit("\"");
      } else {
        emit(entries[i].val);      // number or bool – no quotes
      }
    }
    emit("}");
    buf[pos] = '\0';
  }

  int count() const { return _count; }
};
