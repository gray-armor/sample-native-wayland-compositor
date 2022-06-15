#include "native.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct udev_device*
find_primary_gpu(struct udev* udev, const char* seat)
{
  struct udev_enumerate* e;
  struct udev_list_entry* entry;
  const char *path, *id;
  struct udev_device *device, *drm_device, *pci;

  (void)seat;

  e = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(e, "drm");
  udev_enumerate_add_match_sysname(e, "card[0-9]*");

  udev_enumerate_scan_devices(e);
  drm_device = NULL;

  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e))
  {
    bool is_boot_vga = false;  // trueだとkmsがoffになるのとか関係あるか?

    path = udev_list_entry_get_name(entry);
    device = udev_device_new_from_syspath(udev, path);
    if (!device) continue;

    // weston checks devices' "ID_SEAT" property

    pci = udev_device_get_parent_with_subsystem_devtype(device, "pci", NULL);

    if (pci) {
      id = udev_device_get_sysattr_value(pci, "boot_vga");
      if (id && strcmp(id, "1") == 0) is_boot_vga = true;
    }

    if (!is_boot_vga && drm_device) {
      udev_device_unref(device);
      continue;
    }
  }

  udev_enumerate_unref(e);

  return drm_device;
}

struct zippo_native*
zippo_native_create()
{
  struct zippo_native* self;
  struct udev* udev = NULL;
  struct udev_device* drm_device;

  self = calloc(1, sizeof *self);

  if (self == NULL) {
    fprintf(stderr, "Failed to allocate memory\n");
    goto err;
  }

  udev = udev_new();
  if (udev == NULL) {
    fprintf(stderr, "Failed to initialize udev context\n");
    goto err;
  }

  // TODO: do some stuff regarding logind D-BUS API

  drm_device = find_primary_gpu(udev, "seat0");
  if (drm_device == NULL) {
    fprintf(stderr, "No drm device found\n");
    goto err;
  }

  self->udev = udev;
  self->drm_device = drm_device;

  return self;

err:
  if (udev) udev_unref(udev);

  free(self);

  return NULL;
}

void
zippo_native_destroy(struct zippo_native* self)
{
  udev_device_unref(self->drm_device);
  udev_unref(self->udev);
  free(self);
}
