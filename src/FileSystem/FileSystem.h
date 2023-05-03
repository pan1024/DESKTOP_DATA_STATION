#include <Arduino.h>
#include "FS.h"
String readFile(fs::FS &fs,const char *path);
bool writeFile(fs::FS &fs,const char *path, const char *message);
bool appendFile(fs::FS &fs,const char *path, const char *message);
bool renameFile(fs::FS &fs,const char *path1, const char *path2);
bool deleteFile(fs::FS &fs,const char *path);