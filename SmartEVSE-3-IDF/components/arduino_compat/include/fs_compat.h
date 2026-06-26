/*
 * fs_compat.h — Arduino FS / LittleFS shim
 *
 * The v3 source includes <FS.h> and <LittleFS.h> but only uses them
 * transitively through other libraries (not directly in v3 code paths).
 * We provide stub headers to satisfy the includes.
 */
#ifndef FS_COMPAT_H
#define FS_COMPAT_H

#include <string>
#include "arduino_compat_base.h"   /* for String */

class File {
public:
    bool     isValid() const { return false; }
    operator bool()    const { return false; }
    size_t   size()    const { return 0; }
    size_t   print(const char *s) { (void)s; return 0; }
    size_t   print(const String &s) { (void)s; return 0; }
    size_t   println(const char *s) { (void)s; return 0; }
    size_t   println(const String &s) { (void)s; return 0; }
    String   name()    const { return String(""); }
    String   readString() { return String(""); }
    int      read()    { return -1; }
    void     close()   {}
};

class FS {
public:
    bool begin(bool formatOnFail = false) { (void)formatOnFail; return true; }
    File open(const char *path, const char *mode = "r") { (void)path; (void)mode; return File(); }
    bool exists(const char *path) { (void)path; return false; }
    bool remove(const char *path) { (void)path; return false; }
};

class LittleFSClass : public FS {
public:
    bool begin(bool formatOnFail = false) { return FS::begin(formatOnFail); }
};
extern LittleFSClass LittleFS;

#endif
