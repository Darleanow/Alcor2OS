#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
  if(argc < 2) {
    std::cout << "Usage: edi <path/to/file>\n";
    return 0;
  }

  std::fstream f(argv[1], std::fstream::in | std::fstream::out);
  if(!f.is_open()) {
    std::cout << "Invalid path: " << argv[1] << '\n';
    return 0;
  }

  for(;;) {
    int c = getchar();
    if(c == EOF) {
      break;
    }
    if(putchar(c) == EOF) {
      break;
    }
    if(c == '.') {
      break;
    }
  }

  std::string line;
  while(std::getline(f, line)) {
    std::cout << line << '\n';
  }

  return 0;
}
