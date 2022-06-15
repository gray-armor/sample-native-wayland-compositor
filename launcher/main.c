#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "launch.h"

static void
help(char *name)
{
  fprintf(stderr,
      "Usage: %s [args...] [-- [zippo args..]]\n"
      "  -u, --user      Start session as specified username,\n"
      "                  e.g. -u joe, requires root.\n"
      "  -t, --tty       Start session on alternative tty,\n"
      "                  e.g. -t /dev/tty4, requires -u option.\n"
      "  -h, --help      Display this help message\n",
      name);
}

int
main(int argc, char *argv[])
{
  int i, c, ret;
  struct zippo_launch *launch;
  struct option opts[] = {
      {"user", required_argument, NULL, 'u'},
      {"tty", required_argument, NULL, 't'},
      {"help", no_argument, NULL, 'h'},
      {0, 0, NULL, 0},
  };
  char *user = NULL, *tty = NULL;

  while ((c = getopt_long(argc, argv, "u:t:vh", opts, &i)) != -1) {
    switch (c) {
      case 'u':
        user = optarg;
        if (getuid() != 0) {
          fprintf(stderr, "Permission denied. -u allowed for root only\n");
          exit(EXIT_FAILURE);
        }
        break;

      case 't':
        tty = optarg;
        break;

      case 'h':
        help(argv[0]);
        exit(EXIT_SUCCESS);
        break;

      default:
        exit(EXIT_FAILURE);
        break;
    }
  }

  if (tty && !user) {
    fprintf(stderr, "-t/--tty option requires -u/--user option as well\n");
    exit(EXIT_FAILURE);
  }

  launch = zippo_launch_create(user, tty);
  if (launch == NULL) return EXIT_FAILURE;

  ret = zippo_launch_launch(launch, argc - optind, argv + optind);

  zippo_launch_destroy(launch);

  if (ret != 0) {
    return EXIT_FAILURE;
  } else {
    fprintf(stderr, "Exit successfully\n");
    return EXIT_SUCCESS;
  }
}
