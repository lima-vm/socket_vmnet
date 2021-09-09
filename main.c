#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>

#include <arpa/inet.h>

#include <vmnet/vmnet.h>

#include <libvdeplug.h>

#include "cli.h"

#if __MAC_OS_X_VERSION_MAX_ALLOWED < 101500
#error "Requires macOS 10.15 or later"
#endif

static bool debug = false;

#define DEBUGF(fmt, ...)                                                       \
  do {                                                                         \
    if (debug)                                                                 \
      fprintf(stderr, "DEBUG| " fmt "\n", __VA_ARGS__);                        \
  } while (0)

static void print_vmnet_status(const char *func, vmnet_return_t v) {
  switch (v) {
  case VMNET_SUCCESS:
    DEBUGF("%s(): vmnet_return_t VMNET_SUCCESS", func);
    break;
  case VMNET_FAILURE:
    fprintf(stderr, "%s(): vmnet_return_t VMNET_FAILURE\n", func);
    break;
  case VMNET_MEM_FAILURE:
    fprintf(stderr, "%s(): vmnet_return_t VMNET_MEM_FAILURE\n", func);
    break;
  case VMNET_INVALID_ARGUMENT:
    fprintf(stderr, "%s(): vmnet_return_t VMNET_INVALID_ARGUMENT\n", func);
    break;
  case VMNET_SETUP_INCOMPLETE:
    fprintf(stderr, "%s(): vmnet_return_t VMNET_SETUP_INCOMPLETE\n", func);
    break;
  case VMNET_INVALID_ACCESS:
    fprintf(stderr, "%s(): vmnet_return_t VMNET_INVALID_ACCESS\n", func);
    break;
  case VMNET_PACKET_TOO_BIG:
    fprintf(stderr, "%s(): vmnet_return_t VMNET_PACKET_TOO_BIG\n", func);
    break;
  case VMNET_BUFFER_EXHAUSTED:
    fprintf(stderr, "%s(): vmnet_return_t VMNET_BUFFER_EXHAUSTED\n", func);
    break;
  case VMNET_TOO_MANY_PACKETS:
    fprintf(stderr, "%s(): vmnet_return_t VMNET_TOO_MANY_PACKETS\n", func);
    break;
  default:
    fprintf(stderr, "%s(): vmnet_return_t %d\n", func, v);
    break;
  }
}

static void print_vmnet_start_param(xpc_object_t param) {
  if (param == NULL)
    return;
  xpc_dictionary_apply(param, ^bool(const char *key, xpc_object_t value) {
    xpc_type_t t = xpc_get_type(value);
    if (t == XPC_TYPE_UINT64)
      printf("* %s: %lld\n", key, xpc_uint64_get_value(value));
    else if (t == XPC_TYPE_INT64)
      printf("* %s: %lld\n", key, xpc_int64_get_value(value));
    else if (t == XPC_TYPE_STRING)
      printf("* %s: %s\n", key, xpc_string_get_string_ptr(value));
    else if (t == XPC_TYPE_UUID) {
      char uuid_str[36 + 1];
      uuid_unparse(xpc_uuid_get_bytes(value), uuid_str);
      printf("* %s: %s\n", key, uuid_str);
    } else
      printf("* %s: (unknown type)\n", key);
    return true;
  });
}

static void _on_vmnet_packets_available(interface_ref iface, int64_t buf_count,
                                        int64_t max_bytes, VDECONN *vdeconn) {
  DEBUGF("Receiving from VMNET (buffer for %lld packets, max: %lld "
         "bytes)",
         buf_count, max_bytes);
  // TODO: use prealloced pool
  struct vmpktdesc *pdv = calloc(buf_count, sizeof(struct vmpktdesc));
  if (pdv == NULL) {
    perror("calloc(estim_count, sizeof(struct vmpktdesc)");
    goto done;
  }
  for (int i = 0; i < buf_count; i++) {
    pdv[i].vm_flags = 0;
    pdv[i].vm_pkt_size = max_bytes;
    pdv[i].vm_pkt_iovcnt = 1, pdv[i].vm_pkt_iov = malloc(sizeof(struct iovec));
    if (pdv[i].vm_pkt_iov == NULL) {
      perror("malloc(sizeof(struct iovec))");
      goto done;
    }
    pdv[i].vm_pkt_iov->iov_base = malloc(max_bytes);
    if (pdv[i].vm_pkt_iov->iov_base == NULL) {
      perror("malloc(max_bytes)");
      goto done;
    }
    pdv[i].vm_pkt_iov->iov_len = max_bytes;
  }
  int received_count = buf_count;
  vmnet_return_t read_status = vmnet_read(iface, pdv, &received_count);
  print_vmnet_status(__FUNCTION__, read_status);
  if (read_status != VMNET_SUCCESS) {
    perror("vmnet_read");
    goto done;
  }

  DEBUGF(
      "Received from VMNET: %d packets (buffer was prepared for %lld packets)",
      received_count, buf_count);
  for (int i = 0; i < received_count; i++) {
    DEBUGF("[Handler i=%d] Sending to VDE: %ld bytes", i, pdv[i].vm_pkt_size);
    ssize_t written =
        vde_send(vdeconn, pdv[i].vm_pkt_iov->iov_base, pdv[i].vm_pkt_size, 0);
    DEBUGF("[Handler i=%d] Sent to VDE: %ld bytes", i, written);
    if (written != (ssize_t)pdv[i].vm_pkt_size) {
      perror("vde_send");
      goto done;
    }
  }
done:
  if (pdv != NULL) {
    for (int i = 0; i < buf_count; i++) {
      if (pdv[i].vm_pkt_iov != NULL) {
        if (pdv[i].vm_pkt_iov->iov_base != NULL) {
          free(pdv[i].vm_pkt_iov->iov_base);
        }
        free(pdv[i].vm_pkt_iov);
      }
    }
    free(pdv);
  }
}

#define MAX_PACKET_COUNT_AT_ONCE 32
static void on_vmnet_packets_available(interface_ref iface, int64_t estim_count,
                                       int64_t max_bytes, VDECONN *vdeconn) {
  int64_t q = estim_count / MAX_PACKET_COUNT_AT_ONCE;
  int64_t r = estim_count % MAX_PACKET_COUNT_AT_ONCE;
  DEBUGF("estim_count=%lld, dividing by MAX_PACKET_COUNT_AT_ONCE=%d; q=%lld, "
         "r=%lld",
         estim_count, MAX_PACKET_COUNT_AT_ONCE, q, r);
  for (int i = 0; i < q; i++) {
    _on_vmnet_packets_available(iface, q, max_bytes, vdeconn);
  }
  if (r > 0)
    _on_vmnet_packets_available(iface, r, max_bytes, vdeconn);
}

static interface_ref start(VDECONN *vdeconn, struct cli_options *cliopt) {
  printf("Initializing vmnet.framework (mode %d)\n", cliopt->vmnet_mode);
  xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
  xpc_dictionary_set_uint64(dict, vmnet_operation_mode_key, cliopt->vmnet_mode);
  if (cliopt->vmnet_interface != NULL) {
    printf("Using network interface \"%s\"\n", cliopt->vmnet_interface);
    xpc_dictionary_set_string(dict, vmnet_shared_interface_name_key,
                              cliopt->vmnet_interface);
  }
  if (cliopt->vmnet_gateway != NULL) {
    xpc_dictionary_set_string(dict, vmnet_start_address_key,
                              cliopt->vmnet_gateway);
    xpc_dictionary_set_string(dict, vmnet_end_address_key,
                              cliopt->vmnet_dhcp_end);
    xpc_dictionary_set_string(dict, vmnet_subnet_mask_key, cliopt->vmnet_mask);
  }

  xpc_dictionary_set_uuid(dict, vmnet_interface_id_key,
                          cliopt->vmnet_interface_id);

  dispatch_queue_t q = dispatch_queue_create(
      "io.github.lima-vm.vde_vmnet.start", DISPATCH_QUEUE_SERIAL);
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);

  __block interface_ref iface;
  __block vmnet_return_t status;

  __block uint64_t max_bytes = 0;
  iface = vmnet_start_interface(
      dict, q, ^(vmnet_return_t x_status, xpc_object_t x_param) {
        status = x_status;
        if (x_status == VMNET_SUCCESS) {
          print_vmnet_start_param(x_param);
          max_bytes =
              xpc_dictionary_get_uint64(x_param, vmnet_max_packet_size_key);
        }
        dispatch_semaphore_signal(sem);
      });
  dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
  print_vmnet_status(__FUNCTION__, status);
  dispatch_release(q);
  xpc_release(dict);
  if (status != VMNET_SUCCESS) {
    return NULL;
  }

  dispatch_queue_t event_q = dispatch_queue_create(
      "io.github.lima-vm.vde_vmnet.events", DISPATCH_QUEUE_SERIAL);
  vmnet_interface_set_event_callback(
      iface, VMNET_INTERFACE_PACKETS_AVAILABLE, event_q,
      ^(interface_event_t __attribute__((unused)) x_event_id,
        xpc_object_t x_event) {
        uint64_t estim_count = xpc_dictionary_get_uint64(
            x_event, vmnet_estimated_packets_available_key);
        on_vmnet_packets_available(iface, estim_count, max_bytes, vdeconn);
      });

  return iface;
}

static void stop(interface_ref iface) {
  if (iface == NULL) {
    return;
  }
  dispatch_queue_t q = dispatch_queue_create("io.github.lima-vm.vde_vmnet.stop",
                                             DISPATCH_QUEUE_SERIAL);
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  __block vmnet_return_t status;
  vmnet_stop_interface(iface, q, ^(vmnet_return_t x_status) {
    status = x_status;
    dispatch_semaphore_signal(sem);
  });
  dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
  print_vmnet_status(__FUNCTION__, status);
  dispatch_release(q);
  // TODO: release event_q ?
}

int main(int argc, char *argv[]) {
  debug = getenv("DEBUG") != NULL;
  int rc = 1;
  VDECONN *vdeconn = NULL;
  __block interface_ref iface = NULL;
  void *buf = NULL;
  struct cli_options *cliopt = cli_options_parse(argc, argv);
  assert(cliopt != NULL);
  if (geteuid() != 0) {
    fprintf(stderr, "WARNING: Running without root. This is very unlikely to "
                    "work. See README.md .\n");
  }
  if (geteuid() != getuid()) {
    fprintf(stderr, "WARNING: Seems running with SETUID. This is insecure and "
                    "highly discouraged. See README.md .\n");
  }
  int pid_fd = -1;
  if (cliopt->pidfile != NULL) {
    pid_fd = open(cliopt->pidfile, O_WRONLY | O_CREAT | O_EXLOCK | O_TRUNC | O_NONBLOCK, 0644);
    if (pid_fd == -1) {
      perror("pidfile_open");
      goto done;
    }
  }
  DEBUGF("Opening VDE \"%s\" (for UNIX group \"%s\")", cliopt->vde_switch,
         cliopt->vde_group);
  struct vde_open_args vdeargs = {
      .port = 0, // VDE switch port number, not TCP port number
      .group = cliopt->vde_group,
      .mode = 0770,
  };
  vdeconn = vde_open(cliopt->vde_switch, "vde_vmnet", &vdeargs);
  if (vdeconn == NULL) {
    perror("vde_open");
    goto done;
  }

  iface = start(vdeconn, cliopt);
  if (iface == NULL) {
    perror("start");
    goto done;
  }

  size_t buf_len = 64 * 1024;
  buf = malloc(buf_len);
  if (buf == NULL) {
    perror("malloc");
    goto done;
  }
  if (pid_fd != -1 ) {
    char pid[20];
    snprintf(pid, sizeof(pid), "%u", getpid());
    if (write(pid_fd, pid, strlen(pid)) != (ssize_t)strlen(pid)) {
      perror("pidfile_write");
      goto done;
    }
  }
  for (uint64_t i = 0;; i++) {
    DEBUGF("[VDE-to-VMNET i=%lld] Receiving from VDE", i);
    ssize_t received = vde_recv(vdeconn, buf, buf_len, 0);
    if (received < 0) {
      perror("vde_recv");
      goto done;
    }
    DEBUGF("[VDE-to-VMNET i=%lld] Received from VDE: %ld bytes", i, received);
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = received,
    };
    struct vmpktdesc pd = {
        .vm_pkt_size = received,
        .vm_pkt_iov = &iov,
        .vm_pkt_iovcnt = 1,
        .vm_flags = 0,
    };
    int written_count = pd.vm_pkt_iovcnt;
    DEBUGF("[VDE-to-VMNET i=%lld] Sending to VMNET: %ld bytes", i,
           pd.vm_pkt_size);
    vmnet_return_t write_status = vmnet_write(iface, &pd, &written_count);
    print_vmnet_status(__FUNCTION__, write_status);
    if (write_status != VMNET_SUCCESS) {
      perror("vmnet_write");
      goto done;
    }
    DEBUGF("[VDE-to-VMNET i=%lld] Sent to VMNET: %ld bytes", i, pd.vm_pkt_size);
  }
  rc = 0;
done:
  DEBUGF("shutting down with rc=%d", rc);
  if (iface != NULL) {
    stop(iface);
  }
  if (vdeconn != NULL) {
    vde_close(vdeconn);
  }
  if (pid_fd != -1) {
    unlink(cliopt->pidfile);
    close(pid_fd);
  }
  if (buf != NULL) {
    free(buf);
  }
  cli_options_destroy(cliopt);
  return rc;
}
