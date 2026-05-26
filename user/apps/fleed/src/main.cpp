/**
 * @file fleed/src/main.cpp
 * @brief Fleed - terminal text editor for Alcor2.
 */

#include <fleed/Editor.hpp>

#include <cstdio>
#include <grendizer.h>
#include <iostream>

int main(int argc, char **argv)
{
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"fleed", "<file>", opts, nullptr};
  gr_rest rest;
  char    errbuf[128];

  int     rc = gr_parse(&spec, argc, argv, &rest, errbuf, sizeof errbuf);
  if(rc == GR_HELP)
    return 0;
  if(rc != GR_OK) {
    std::cerr << errbuf << '\n';
    return 1;
  }
  if(rest.argc < 1) {
    gr_usage(&spec, stderr);
    return 1;
  }

  return fleed::Editor(rest.argv[0]).run();
}
