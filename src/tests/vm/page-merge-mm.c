#include "tests/main.h"
#include "tests/vm/parallel-merge.h"
#include <debug.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  parallel_merge ("child-qsort-mm", 80);
}
