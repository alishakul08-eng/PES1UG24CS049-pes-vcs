#include "pes.h"
#include "index.h"
#include <stdio.h>
#include <string.h>
int main() {
  Index idx;
  memset(&idx, 0, sizeof(Index));
  printf("Attempting direct add...\n");
  if(index_add(&idx, "file1.txt") == 0 && index_add(&idx, "file2.txt") == 0) {
    printf("Success! Files staged.\n");
  } else {
    printf("Addition failed.\n");
  }
  return 0;
}