#define _GNU_SOURCE
#include <dirent.h>
#include <string.h>

int versionsort(const struct dirent** a, const struct dirent** b) {
  return strverscmp((*a)->d_name, (*b)->d_name);
}
