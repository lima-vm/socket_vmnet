#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>

#include <availability.h>

#include "cli.h"

#ifndef VERSION
#define VERSION "UNKNOWN"
#endif

#if __MAC_OS_X_VERSION_MAX_ALLOWED < 101500
#error "Requires macOS 10.15 or later"
#endif

#define CLI_DEFAULT_VDE_GROUP "staff"

static void print_usage(const char *argv0) {
  printf("Usage: %s [OPTION]... VDESWITCH\n", argv0);
  printf("vmnet.framework support for rootless QEMU.\n");
  printf("vde_vmnet does not require QEMU to run as the root user, but "
         "vde_vmnet itself has to run as the root, in most cases.\n");
  printf("\n");
  printf("--vde-group=GROUP                   VDE group name (default: "
         "\"" CLI_DEFAULT_VDE_GROUP "\")\n");
  printf(
      "--vmnet-mode=(host|shared|bridged)  vmnet mode (default: \"shared\")\n");
  printf("--vmnet-interface=INTERFACE         interface used for "
         "--vmnet=bridged, e.g., \"en0\"\n");
  printf("-h, --help                          display this help and exit\n");
  printf("-v, --version                       display version information and "
         "exit\n");
  printf("\n");
  printf("version: " VERSION "\n");
}

static void print_version() { puts(VERSION); }

#define CLI_OPTIONS_ID_VDE_GROUP -42
#define CLI_OPTIONS_ID_VMNET_MODE -43
#define CLI_OPTIONS_ID_VMNET_INTERFACE -44
struct cli_options *cli_options_parse(int argc, char *argv[]) {
  struct cli_options *res = malloc(sizeof(*res));
  if (res == NULL) {
    goto error;
  }
  memset(res, 0, sizeof(*res));

  const struct option longopts[] = {
      {"vde-group", required_argument, NULL, CLI_OPTIONS_ID_VDE_GROUP},
      {"vmnet-mode", required_argument, NULL, CLI_OPTIONS_ID_VMNET_MODE},
      {"vmnet-interface", required_argument, NULL,
       CLI_OPTIONS_ID_VMNET_INTERFACE},
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, 'v'},
      {0, 0, 0, 0},
  };
  int opt = 0;
  while ((opt = getopt_long(argc, argv, "hv", longopts, NULL)) != -1) {
    switch (opt) {
    case CLI_OPTIONS_ID_VDE_GROUP:
      res->vde_group = strdup(optarg);
      break;
    case CLI_OPTIONS_ID_VMNET_MODE:
      if (strcmp(optarg, "host") == 0) {
        res->vmnet_mode = VMNET_HOST_MODE;
      } else if (strcmp(optarg, "shared") == 0) {
        res->vmnet_mode = VMNET_SHARED_MODE;
      } else if (strcmp(optarg, "bridged") == 0) {
        res->vmnet_mode = VMNET_BRIDGED_MODE;
      } else {
        fprintf(stderr, "Unknown vmnet mode \"%s\"\n", optarg);
        goto error;
      }
      break;
    case CLI_OPTIONS_ID_VMNET_INTERFACE:
      res->vmnet_interface = strdup(optarg);
      break;
    case 'h':
      print_usage(argv[0]);
      cli_options_destroy(res);
      exit(EXIT_SUCCESS);
      return NULL;
      break;
    case 'v':
      print_version();
      cli_options_destroy(res);
      exit(EXIT_SUCCESS);
      return NULL;
      break;
    default:
      goto error;
      break;
    }
  }
  if (argc - optind != 1) {
    goto error;
  }
  res->vde_switch = strdup(argv[optind]);

  /* fill default */
  if (res->vde_group == NULL)
    res->vde_group =
        strdup(CLI_DEFAULT_VDE_GROUP); /* use strdup to make it freeable */
  if (res->vmnet_mode == 0)
    res->vmnet_mode = VMNET_SHARED_MODE;

  /* validate */
  if (res->vmnet_mode == VMNET_BRIDGED_MODE && res->vmnet_interface == NULL) {
    fprintf(
        stderr,
        "vmnet mode \"bridged\" require --vmnet-interface to be specified\n");
    goto error;
  }
  return res;
error:
  print_usage(argv[0]);
  cli_options_destroy(res);
  exit(EXIT_FAILURE);
  return NULL;
}

void cli_options_destroy(struct cli_options *x) {
  if (x == NULL)
    return;
  if (x->vde_group != NULL)
    free(x->vde_group);
  if (x->vde_switch != NULL)
    free(x->vde_switch);
  if (x->vmnet_interface != NULL)
    free(x->vmnet_interface);
  free(x);
}
