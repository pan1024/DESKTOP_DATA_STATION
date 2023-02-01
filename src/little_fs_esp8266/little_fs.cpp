#include<little_fs_esp8266/little_fs.h>
String readFile(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) return "nullptr";
  String result="";
  while(file.available()) result+=(char)file.read();
  return result;
}

boolean writeFile(const char *path, const char *message) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  if (file.print(message)) return true;
  else return false;
  file.close();
}

boolean appendFile(const char *path, const char *message) {
  File file = LittleFS.open(path, "a");
  if (!file) return false;
  if (file.print(message)) return true;
  else return false;
  file.close();
}

boolean renameFile(const char *path1, const char *path2) {
  if (LittleFS.rename(path1, path2)) return true;
  else return false;
}

boolean deleteFile(const char *path) {
  if (LittleFS.remove(path)) return true;
  else return false;
}