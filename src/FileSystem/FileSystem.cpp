#include <FileSystem\FileSystem.h>


String readFile(fs::FS &fs, const char *path) {
  File file = fs.open(path, "r");
  if (!file) return "nullptr";
  String result="";
  while(file.available()) result+=(char)file.read();
  file.close();
  return result;
}

bool writeFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, "w");
  if (!file) return false;
  if (file.print(message)) {
    file.close();
    return true;
  }
  else{
    file.close();
    return false;
  } 
}

bool appendFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, "a");
  if (!file) return false;
  if (file.print(message)){
    file.close();
    return true;
  }
  else{
    file.close();
    return false;
  } 
  
}

bool renameFile(fs::FS &fs, const char *path1, const char *path2) {
  if (fs.rename(path1, path2)) return true;
  else return false;
}

bool deleteFile(fs::FS &fs, const char *path) {
  if (fs.remove(path)) return true;
  else return false;
}