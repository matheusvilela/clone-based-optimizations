#include <stdlib.h>
#include <stdio.h>

int *p;
int main(int argc, char** argv) {
  p = (int*)malloc(sizeof(int));

  if (argc) {
    *p = 0;
  } else {
    *p = 1;
  }


  int x = *p;
}
