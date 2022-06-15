#include <errno.h>
#include <fcntl.h>
#include <linux/vt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

void
printb(unsigned short v)
{
  unsigned short mask = (int)1 << (sizeof(v) * __CHAR_BIT__ - 1);
  do fputc(mask & v ? '1' : '0', stderr);
  while (mask >>= 1);
}

void
vt_mode_test()
{
  int tty = STDIN_FILENO;
  struct vt_mode vtm;

  // get current mode
  if (ioctl(tty, VT_GETMODE, &vtm) < 0) {
    fprintf(stderr, "Failed to get mode: %s\n", strerror(errno));
  } else {
    switch (vtm.mode) {
      case VT_AUTO:
        fprintf(stderr, "vt_mode: VT_AUTO\n");
        break;
      case VT_PROCESS:
        fprintf(stderr, "vt_mode: VT_PROCESS\n");
        break;
      case VT_ACKACQ:
        fprintf(stderr, "vt_mode: VT_ACKACQ\n");
        break;
    }
  }

  // change to vt_process mode
  struct vt_mode new_mode = {
      .mode = VT_PROCESS, .waitv = 0, .relsig = 0, .acqsig = 0, .frsig = 0};

  if (ioctl(tty, VT_SETMODE, &new_mode) < 0) {
    fprintf(stderr, "Failed to set vt mode: %s\n", strerror(errno));
  } else {
    fprintf(stderr, "Change vt mode to VT_PROCESS\n");
  }

  fprintf(stderr, "Press enter to proceed.\n");
  fgetc(stdin);

  // reset
  if (ioctl(tty, VT_SETMODE, &vtm) < 0) {
    fprintf(stderr, "Failed to set vt mode: %s\n", strerror(errno));
  } else {
    fprintf(stderr, "Reset vt mode\n");
  }

  fprintf(stderr, "Press enter to proceed.\n");
  fgetc(stdin);
}

void
vt_stat_test()
{
  struct vt_stat vts;
  int tty = STDIN_FILENO;
  int another_tty, another_tty_num;

  if (ioctl(tty, VT_GETSTATE, &vts) < 0) {
    fprintf(stderr, "Failed to get stat: %s\n", strerror(errno));
  } else {
    fprintf(stderr, "active: %d\nmask: ", vts.v_active);
    printb(vts.v_state);
    fprintf(stderr, "\n");
  }

  if (ioctl(tty, VT_OPENQRY, &another_tty_num) < 0 || another_tty_num < 0) {
    fprintf(stderr, "Failed to find unused tty: %s\n", strerror(errno));
  } else {
    char path[32];
    snprintf(path, 32, "/dev/tty%d", another_tty_num);
    another_tty = open(path, O_RDWR | O_NOCTTY);
    if (another_tty < 0) {
      fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
      return;
    }
    fprintf(stderr, "open %s\n", path);
  }

  if (ioctl(tty, VT_GETSTATE, &vts) < 0) {
    fprintf(stderr, "Failed to get stat: %s\n", strerror(errno));
  } else {
    fprintf(stderr, "active: %d\nmask: ", vts.v_active);
    printb(vts.v_state);
    fprintf(stderr, "\n");
  }

  close(another_tty);
}

int
main(int argc, char const *argv[])
{
  if (argc <= 1) return EXIT_FAILURE;

  if (strcmp(argv[1], "mode") == 0) {
    vt_mode_test();
  } else if (strcmp(argv[1], "stat") == 0) {
    vt_stat_test();
  } else {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
