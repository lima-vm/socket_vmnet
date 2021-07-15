#ifndef VDE_VMNET_CLI_H
#define VDE_VMNET_CLI_H

struct cli_options {
  char *vde_group;  // --vde-group
  char *vde_switch; // arg
};

struct cli_options *cli_options_parse(int argc, char *argv[]);
void cli_options_destroy(struct cli_options *);

#endif /* VDE_VMNET_CLI_H */
