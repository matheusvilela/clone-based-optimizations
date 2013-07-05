#include <stdlib.h>
#include <stdio.h>

//Analysis cannot remove the stores
//as the definition that reaches the use
//can come from any of the stores
int main(int argc, char** argv) {
  int *p = (int*)malloc(sizeof(int));

  if (argc) {
    *p = 0;
  } else {
    *p = 1;
  }


  int x = *p;
}
