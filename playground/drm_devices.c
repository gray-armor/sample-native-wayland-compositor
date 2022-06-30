#include <libudev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
print_error(const char* msg)
{
  fprintf(stdout, "Invalid command line arguments: %s\n", msg);
  return -1;
}

static int
process_device(
    struct udev_device* device, int argc, char const* argv[], int index)
{
  int argc_remain = argc;
  char const** argv_remain = argv;
  const char* syspath;

  syspath = udev_device_get_syspath(device);
  if (index >= 0)
    fprintf(stdout, "[%2d] %s\n", index, syspath);
  else
    fprintf(stdout, "[xx] %s\n", syspath);

  while (argc_remain > 0) {
    int forward = 0;
    if (strcmp(argv_remain[0], "--devtype") == 0) {
      fprintf(stdout, "devtype: %s\n", udev_device_get_devtype(device));
      forward = 1;
    } else if (strcmp(argv_remain[0], "--devnode") == 0) {
      fprintf(stdout, "devnode: %s\n", udev_device_get_devnode(device));
      forward = 1;
    } else if (strcmp(argv_remain[0], "--tags") == 0) {
      struct udev_list_entry *list, *entry;
      list = udev_device_get_tags_list_entry(device);

      fprintf(stdout, "tags:\n");
      udev_list_entry_foreach(entry, list)
      {
        const char* name = udev_list_entry_get_name(entry);
        fprintf(stdout, "\t%s\n", name);
      }

      forward = 1;
    } else if (strcmp(argv_remain[0], "--properties") == 0) {
      struct udev_list_entry *list, *entry;
      list = udev_device_get_properties_list_entry(device);

      fprintf(stdout, "properties:\n");
      udev_list_entry_foreach(entry, list)
      {
        const char* name = udev_list_entry_get_name(entry);
        fprintf(stdout, "\t%s\n", name);
      }

      forward = 1;
    } else if (strcmp(argv_remain[0], "--sysattrs") == 0) {
      struct udev_list_entry *list, *entry;
      list = udev_device_get_sysattr_list_entry(device);

      fprintf(stdout, "sysattrs:\n");
      udev_list_entry_foreach(entry, list)
      {
        const char* name = udev_list_entry_get_name(entry);
        fprintf(stdout, "\t%s\n", name);
      }

      forward = 1;
    } else if (strcmp(argv_remain[0], "--property") == 0) {
      const char *property, *key;
      if (argc_remain < 2) return print_error("no tag name given");
      key = argv_remain[1];
      property = udev_device_get_property_value(device, key);
      fprintf(stdout, "property \"%s\": %s\n", key, property);
      forward = 2;
    } else if (strcmp(argv_remain[0], "--sysattr") == 0) {
      const char *attr, *key;
      if (argc_remain < 2) return print_error("no sysattr name given");
      key = argv_remain[1];
      attr = udev_device_get_sysattr_value(device, key);
      fprintf(stdout, "sysattr \"%s\": %s\n", key, attr);
      forward = 2;
    } else if (strcmp(argv_remain[0], "--parent-with-subsystem") == 0) {
      const char* key;
      struct udev_device* parent;
      if (argc_remain < 2) return print_error("no subsystem given");
      key = argv_remain[1];
      parent = udev_device_get_parent_with_subsystem_devtype(device, key, NULL);
      if (parent != NULL) {
        process_device(parent, argc_remain - 2, argv_remain + 2, -1);
        udev_device_unref(parent);
      }
      return 0;
    } else {
      return print_error("invalid device option\n");
    }
    argc_remain -= forward;
    argv_remain += forward;
  }

  return 0;
}

static int
process_device_list(struct udev* udev, struct udev_list_entry* list, int argc,
    char const* argv[])
{
  struct udev_list_entry* entry;
  struct udev_device* device;
  int i = -1, argc_remain = argc;
  char const** argv_remain = argv;
  long n;
  bool has_n = false;

  if (argc_remain > 1 && strcmp(argv_remain[0], "-n") == 0) {
    char* end;
    n = strtol(argv_remain[1], &end, 0);
    if (*end != '\0') return print_error("invalid index");
    argc_remain -= 2;
    argv_remain += 2;
    has_n = true;
  }

  udev_list_entry_foreach(entry, list)
  {
    const char* path;

    i++;
    if (has_n && i != n) continue;

    path = udev_list_entry_get_name(entry);

    device = udev_device_new_from_syspath(udev, path);
    if (!device) return print_error("failed to get device");

    int ret = process_device(device, argc_remain, argv_remain, i);

    if (ret != 0 || has_n) return ret;
  }

  return has_n ? print_error("index out of range") : 0;
}

static int
process_root(struct udev* udev, int argc, char const* argv[])
{
  struct udev_enumerate* e;
  struct udev_list_entry* list;
  int argc_remain = argc;
  char const** argv_remain = argv;

  e = udev_enumerate_new(udev);

  while (1) {
    if (argc_remain < 2) break;
    if (strcmp("--subsystem", argv_remain[0]) == 0)
      udev_enumerate_add_match_subsystem(e, argv_remain[1]);
    else if (strcmp("--sysname", argv_remain[0]) == 0)
      udev_enumerate_add_match_sysname(e, argv_remain[1]);
    else
      break;

    argc_remain -= 2;
    argv_remain += 2;
  }

  udev_enumerate_scan_devices(e);
  list = udev_enumerate_get_list_entry(e);

  return process_device_list(udev, list, argc_remain, argv_remain);
}

// sample usage
// ./build/playground/drm_devices --subsystem drm
// ./build/playground/drm_devices --subsystem drm -n 0 --devnode --properties
/*
./build/playground/drm_devices --property SUBSYSTEM \
  | grep property \
  | sort - \
  | uniq
*/

int
main(int argc, char const* argv[])
{
  int exit_status = EXIT_FAILURE;
  struct udev* udev = NULL;

  udev = udev_new();
  if (udev == NULL) {
    fprintf(stdout, "Failed to initialize udev context");
    goto err;
  }

  if (process_root(udev, argc - 1, argv + 1)) goto err;

  exit_status = EXIT_SUCCESS;

err:
  if (udev) udev_unref(udev);

  return exit_status;
}
