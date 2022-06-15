#include <stdio.h>

#include "config.h"
#include "native.h"

int
main()
{
  fprintf(stderr, "zippo %s\n", VERSION);

  struct zippo_native *native;

  native = zippo_native_create();

  if (native == NULL) goto err;

  zippo_native_destroy(native);

  return 0;

err:
  return 1;
}
