#pragma once

#include <sys/types.h>

int parse_int(const char *s);
short parse_short(const char *s);
pid_t spawn_child(char **cmd);
int run_child(char **cmd);
