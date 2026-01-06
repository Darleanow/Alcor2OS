/**
 * ls - List directory contents
 *
 * Usage: ls [path]
 */

#include <dirent.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Write string to standard output.
 * @param s String to print.
 */
static void print(const char *s)
{
  write(1, s, strlen(s));
}

int main(int argc, char *argv[])
{
  const char *path = ".";

  if(argc > 1) {
    path = argv[1];
  }

  DIR *dir = opendir(path);
  if(!dir) {
    print("ls: cannot open directory\n");
    return 1;
  }

  struct dirent *entry;
  while((entry = readdir(dir)) != NULL) {
    if(entry->d_type == DT_DIR) {
      print("[DIR]  ");
    } else {
      print("[FILE] ");
    }
    print(entry->d_name);
    print("\n");
  }

  closedir(dir);
  return 0;
}
