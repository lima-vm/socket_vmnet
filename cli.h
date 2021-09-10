#ifndef VDE_VMNET_CLI_H
#define VDE_VMNET_CLI_H

#include <uuid/uuid.h>

#include <vmnet/vmnet.h>

struct cli_options {
  // --vde-group
  char *vde_group;
  // --vmnet-mode, corresponds to vmnet_operation_mode_key
  operating_modes_t vmnet_mode;
  // --vmnet-interface, corresponds to vmnet_shared_interface_name_key
  char *vmnet_interface;
  // --vmnet-gateway, corresponds to vmnet_start_address_key
  char *vmnet_gateway;
  // --vmnet-dhcp-end, corresponds to vmnet_end_address_key
  char *vmnet_dhcp_end;
  // --vmnet-mask, corresponds to vmnet_subnet_mask_key
  char *vmnet_mask;
  // --vmnet-interface-id, corresponds to vmnet_interface_id_key
  uuid_t vmnet_interface_id;
  // -p, --pidfile; writes pidfile using permissions of vde_vmnet
  char *pidfile;
  // arg
  char *vde_switch;
};

struct cli_options *cli_options_parse(int argc, char *argv[]);
void cli_options_destroy(struct cli_options *);

#endif /* VDE_VMNET_CLI_H */
