#pragma once
#include "FS.h"

class SDMMCStub {
public:
  File open(const char *, const char *) { return File(); }
  bool mkdir(const char *) { return true; }
  bool rmdir(const char *) { return true; }
  bool remove(const char *) { return true; }
  bool rename(const char *, const char *) { return true; }
  bool exists(const char *) { return false; }
};

inline SDMMCStub SD_MMC;
