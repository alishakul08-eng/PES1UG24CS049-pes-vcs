#include "pes.h"
#include "index.h"
#include <stdio.h>
int main() {
  Index idx;
  index_load(&idx);
  printf("Adding file1.txt...\n");
  index_add(&idx, "file1.txt");
  printf("Adding file2.txt...\n");
  index_add(&idx, "file2.txt");
  index_save(&idx);
  return 0;
}