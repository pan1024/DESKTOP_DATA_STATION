#include <Arduino.h>
#include "FS.h"
#include <LittleFS.h>
String readFile(const char *path);
boolean writeFile(const char *path, const char *message);
boolean appendFile(const char *path, const char *message);
boolean renameFile(const char *path1, const char *path2);
boolean deleteFile(const char *path);