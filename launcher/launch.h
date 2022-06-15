#ifndef ZIPPO_LAUNCHER_LAUNCH_H
#define ZIPPO_LAUNCHER_LAUNCH_H

enum zippo_launch_opcode {
  ZIPPO_LAUNCH_OPEN,
};

enum zippo_launch_event {
  ZIPPO_LAUNCH_ACTIVATE,
  ZIPPO_LAUNCH_DEACTIVATE,
  ZIPPO_LAUNCH_DEACTIVATE_DONE,
  // this event is followed by an fd handle
  ZIPPO_LAUNCH_OPEN_REPLY,
};

struct zippo_launch_message {
  int opcode;
};

struct zippo_launch;

int zippo_launch_launch(struct zippo_launch* self, int argc, char* argv[]);

struct zippo_launch* zippo_launch_create(const char* user, const char* tty);

void zippo_launch_destroy(struct zippo_launch* self);

#endif  //  ZIPPO_LAUNCHER_LAUNCH_H
