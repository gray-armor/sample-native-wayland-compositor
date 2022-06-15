#ifndef ZIPPO_NATIVE_H
#define ZIPPO_NATIVE_H

#include <libudev.h>

struct zippo_native {
  struct udev* udev;
  struct udev_device* drm_device;
};

struct zippo_native* zippo_native_create();

void zippo_native_destroy(struct zippo_native* self);

#endif  //  ZIPPO_NATIVE_H
