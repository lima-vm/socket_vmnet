#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <sched.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <vmnet/vmnet.h>

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

#define INFOF(fmt, ...)     fprintf(stderr, "INFO | " fmt "\n", __VA_ARGS__)
#define ERROR(msg)          fprintf(stderr, "ERROR| " msg "\n")
#define ERRORF(fmt, ...)    fprintf(stderr, "ERROR| " fmt "\n", __VA_ARGS__)
#define ERRORN(name)        ERRORF(name ": %s", strerror(errno))

static const char *vmnet_strerror(vmnet_return_t v) {
  switch (v) {
  case VMNET_SUCCESS:
    return "VMNET_SUCCESS";
  case VMNET_FAILURE:
    return "VMNET_FAILURE";
  case VMNET_MEM_FAILURE:
    return "VMNET_MEM_FAILURE";
  case VMNET_INVALID_ARGUMENT:
    return "VMNET_INVALID_ARGUMENT";
  case VMNET_SETUP_INCOMPLETE:
    return "VMNET_SETUP_INCOMPLETE";
  case VMNET_INVALID_ACCESS:
    return "VMNET_INVALID_ACCESS";
  case VMNET_PACKET_TOO_BIG:
    return "VMNET_PACKET_TOO_BIG";
  case VMNET_BUFFER_EXHAUSTED:
    return "VMNET_BUFFER_EXHAUSTED";
  case VMNET_TOO_MANY_PACKETS:
    return "VMNET_TOO_MANY_PACKETS";
  default:
    return "(unknown status)";
  }
}

static void print_vmnet_start_param(xpc_object_t param) {
  if (param == NULL)
    return;
  xpc_dictionary_apply(param, ^bool(const char *key, xpc_object_t value) {
    xpc_type_t t = xpc_get_type(value);
    if (t == XPC_TYPE_UINT64)
      INFOF("* %s: %lld", key, xpc_uint64_get_value(value));
    else if (t == XPC_TYPE_INT64)
      INFOF("* %s: %lld", key, xpc_int64_get_value(value));
    else if (t == XPC_TYPE_STRING)
      INFOF("* %s: %s", key, xpc_string_get_string_ptr(value));
    else if (t == XPC_TYPE_UUID) {
      char uuid_str[36 + 1];
      uuid_unparse(xpc_uuid_get_bytes(value), uuid_str);
      INFOF("* %s: %s", key, uuid_str);
    } else
      INFOF("* %s: (unknown type)", key);
    return true;
  });
}

struct conn {
  // TODO: uint8_t mac[6];
  int socket_fd;
  struct conn *next;
} _conn;

struct state {
  dispatch_semaphore_t sem;
  struct conn *conns; // TODO: avoid O(N) lookup
} _state;

static void state_add_socket_fd(struct state *state, int socket_fd) {
  dispatch_semaphore_wait(state->sem, DISPATCH_TIME_FOREVER);
  struct conn *conn = malloc(sizeof(struct conn));
  memset(conn, 0, sizeof(*conn));
  conn->socket_fd = socket_fd;
  if (state->conns == NULL) {
    state->conns = conn;
  } else {
    struct conn *last;
    for (last = state->conns; last->next != NULL; last = last->next)
      ;
    last->next = conn;
  }
  dispatch_semaphore_signal(state->sem);
}

static void state_remove_socket_fd(struct state *state, int socket_fd) {
  dispatch_semaphore_wait(state->sem, DISPATCH_TIME_FOREVER);
  if (state->conns != NULL) {
    if (state->conns->socket_fd == socket_fd) {
      state->conns = state->conns->next;
    } else {
      struct conn *conn;
      for (conn = state->conns; conn->next != NULL; conn = conn->next) {
        if (conn->next->socket_fd == socket_fd) {
          conn->next = conn->next->next;
	  break;
        }
      }
    }
  }
  dispatch_semaphore_signal(state->sem);
}

static void _on_vmnet_packets_available(interface_ref iface, int64_t buf_count,
                                        int64_t max_bytes,
                                        struct state *state) {
  DEBUGF("Receiving from VMNET (buffer for %lld packets, max: %lld "
         "bytes)",
         buf_count, max_bytes);
  // TODO: use prealloced pool
  struct vmpktdesc *pdv = calloc(buf_count, sizeof(struct vmpktdesc));
  if (pdv == NULL) {
    ERRORN("calloc(estim_count, sizeof(struct vmpktdesc)");
    goto done;
  }
  for (int i = 0; i < buf_count; i++) {
    pdv[i].vm_flags = 0;
    pdv[i].vm_pkt_size = max_bytes;
    pdv[i].vm_pkt_iovcnt = 1, pdv[i].vm_pkt_iov = malloc(sizeof(struct iovec));
    if (pdv[i].vm_pkt_iov == NULL) {
      ERRORN("malloc(sizeof(struct iovec))");
      goto done;
    }
    pdv[i].vm_pkt_iov->iov_base = malloc(max_bytes);
    if (pdv[i].vm_pkt_iov->iov_base == NULL) {
      ERRORN("malloc(max_bytes)");
      goto done;
    }
    pdv[i].vm_pkt_iov->iov_len = max_bytes;
  }
  int received_count = buf_count;
  vmnet_return_t read_status = vmnet_read(iface, pdv, &received_count);
  if (read_status != VMNET_SUCCESS) {
    ERRORF("vmnet_read: [%d] %s", read_status, vmnet_strerror(read_status));
    goto done;
  }

  DEBUGF(
      "Received from VMNET: %d packets (buffer was prepared for %lld packets)",
      received_count, buf_count);
  for (int i = 0; i < received_count; i++) {
    uint8_t dest_mac[6], src_mac[6];
    assert(pdv[i].vm_pkt_iov[0].iov_len > 12);
    memcpy(dest_mac, pdv[i].vm_pkt_iov[0].iov_base, sizeof(dest_mac));
    memcpy(src_mac, pdv[i].vm_pkt_iov[0].iov_base + 6, sizeof(src_mac));
    DEBUGF("[Handler i=%d] Dest %02X:%02X:%02X:%02X:%02X:%02X, Src "
           "%02X:%02X:%02X:%02X:%02X:%02X,",
           i, dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4],
           dest_mac[5], src_mac[0], src_mac[1], src_mac[2], src_mac[3],
           src_mac[4], src_mac[5]);
    dispatch_semaphore_wait(state->sem, DISPATCH_TIME_FOREVER);
    struct conn *conns = state->conns;
    dispatch_semaphore_signal(state->sem);
    for (struct conn *conn = conns; conn != NULL; conn = conn->next) {
      // FIXME: avoid flooding
      DEBUGF("[Handler i=%d] Sending to the socket %d: 4 + %ld bytes [Dest "
             "%02X:%02X:%02X:%02X:%02X:%02X]",
             i, conn->socket_fd, pdv[i].vm_pkt_size, dest_mac[0], dest_mac[1],
             dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);
      uint32_t header_be = htonl(pdv[i].vm_pkt_size);
      struct iovec iov[2] = {
          {
              .iov_base = &header_be,
              .iov_len = 4,
          },
          {
              .iov_base = pdv[i].vm_pkt_iov[0].iov_base,
              .iov_len = pdv[i].vm_pkt_size, // not vm_pkt_iov[0].iov_len
          },
      };
      ssize_t written = writev(conn->socket_fd, iov, 2);
      DEBUGF("[Handler i=%d] Sent to the socket: %ld bytes (including uint32be "
             "header)",
             i, written);
      if (written < 0) {
        ERRORN("writev");
        goto done;
      }
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
                                       int64_t max_bytes, struct state *state) {
  int64_t q = estim_count / MAX_PACKET_COUNT_AT_ONCE;
  int64_t r = estim_count % MAX_PACKET_COUNT_AT_ONCE;
  DEBUGF("estim_count=%lld, dividing by MAX_PACKET_COUNT_AT_ONCE=%d; q=%lld, "
         "r=%lld",
         estim_count, MAX_PACKET_COUNT_AT_ONCE, q, r);
  for (int i = 0; i < q; i++) {
    _on_vmnet_packets_available(iface, MAX_PACKET_COUNT_AT_ONCE, max_bytes, state);
  }
  if (r > 0)
    _on_vmnet_packets_available(iface, r, max_bytes, state);
}

static interface_ref start(struct state *state, struct cli_options *cliopt) {
  INFOF("Initializing vmnet.framework (mode %d)", cliopt->vmnet_mode);
  xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
  xpc_dictionary_set_uint64(dict, vmnet_operation_mode_key, cliopt->vmnet_mode);
  if (cliopt->vmnet_interface != NULL) {
    INFOF("Using network interface \"%s\"", cliopt->vmnet_interface);
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

  if (cliopt->vmnet_nat66_prefix != NULL) {
    xpc_dictionary_set_string(dict, vmnet_nat66_prefix_key,
                              cliopt->vmnet_nat66_prefix);
  }

  dispatch_queue_t q = dispatch_queue_create(
      "io.github.lima-vm.socket_vmnet.start", DISPATCH_QUEUE_SERIAL);
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
  dispatch_release(q);
  xpc_release(dict);
  if (status != VMNET_SUCCESS) {
    ERRORF("vmnet_start_interface: [%d] %s", status, vmnet_strerror(status));
    return NULL;
  }

  dispatch_queue_t event_q = dispatch_queue_create(
      "io.github.lima-vm.socket_vmnet.events", DISPATCH_QUEUE_CONCURRENT);
  vmnet_interface_set_event_callback(
      iface, VMNET_INTERFACE_PACKETS_AVAILABLE, event_q,
      ^(interface_event_t __attribute__((unused)) x_event_id,
        xpc_object_t x_event) {
        uint64_t estim_count = xpc_dictionary_get_uint64(
            x_event, vmnet_estimated_packets_available_key);
        on_vmnet_packets_available(iface, estim_count, max_bytes, state);
      });

  return iface;
}

static sigjmp_buf jmpbuf;
static void signalhandler(int signal) {
  INFOF("Received signal: %s", strsignal(signal));
  siglongjmp(jmpbuf, 1);
}

static void stop(interface_ref iface) {
  if (iface == NULL) {
    return;
  }
  dispatch_queue_t q = dispatch_queue_create(
      "io.github.lima-vm.socket_vmnet.stop", DISPATCH_QUEUE_SERIAL);
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  __block vmnet_return_t status;
  vmnet_stop_interface(iface, q, ^(vmnet_return_t x_status) {
    status = x_status;
    dispatch_semaphore_signal(sem);
  });
  dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
  dispatch_release(q);
  // TODO: release event_q ?
  if (status != VMNET_SUCCESS) {
    ERRORF("vmnet_stop_interface: [%d] %s", status, vmnet_strerror(status));
  }
}

static int socket_bindlisten(const char *socket_path,
                             const char *socket_group) {
  int fd = -1;
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  unlink(socket_path); /* avoid EADDRINUSE */
  if ((fd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
    ERRORN("socket");
    goto err;
  }
  addr.sun_family = PF_LOCAL;
  size_t socket_len = strlen(socket_path);
  if (socket_len + 1 > sizeof(addr.sun_path)) {
    ERRORF("the socket path is too long: %zu", socket_len);
    goto err;
  }
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    ERRORN("bind");
    goto err;
  }
  if (listen(fd, 0) < 0) {
    ERRORN("listen");
    goto err;
  }
  if (socket_group != NULL) {
    errno = 0;
    struct group *grp = getgrnam(socket_group); /* Do not free */
    if (grp == NULL) {
      if (errno != 0)
        ERRORN("getgrnam");
      else
        ERRORF("unknown group name \"%s\"", socket_group);
      goto err;
    }
    /* fchown can't be used (EINVAL) */
    if (chown(socket_path, -1, grp->gr_gid) < 0) {
      ERRORN("chown");
      goto err;
    }
    if (chmod(socket_path, 0770) < 0) {
      ERRORN("chmod");
      goto err;
    }
  }
  return fd;
err:
  if (fd >= 0)
    close(fd);
  return -1;
}

static void on_accept(struct state *state, int accept_fd, interface_ref iface);

int main(int argc, char *argv[]) {
  debug = getenv("DEBUG") != NULL;
  int rc = 1, listen_fd = -1;
  __block interface_ref iface = NULL;
  dispatch_queue_t q = dispatch_queue_create(
      "io.github.lima-vm.socket_vmnet.accept", DISPATCH_QUEUE_CONCURRENT);

  struct cli_options *cliopt = cli_options_parse(argc, argv);
  assert(cliopt != NULL);
  if (geteuid() != 0) {
    ERROR("WARNING: Running without root. This is very unlikely to "
          "work: See README.md");
  }
  if (geteuid() != getuid()) {
    ERROR("WARNING: Seems running with SETUID. This is insecure and "
          "highly discouraged: See README.md");
  }

  if (sigsetjmp(jmpbuf, 1) != 0) {
    goto done;
  }
  signal(SIGHUP, signalhandler);
  signal(SIGINT, signalhandler);
  signal(SIGTERM, signalhandler);

  // We will receive EPIPE on the socket.
  signal(SIGPIPE, SIG_IGN);

  int pid_fd = -1;
  if (cliopt->pidfile != NULL) {
    pid_fd = open(cliopt->pidfile,
                  O_WRONLY | O_CREAT | O_EXLOCK | O_TRUNC | O_NONBLOCK, 0644);
    if (pid_fd == -1) {
      ERRORN("pidfile_open");
      goto done;
    }
  }
  DEBUGF("Opening socket \"%s\" (for UNIX group \"%s\")", cliopt->socket_path,
         cliopt->socket_group);
  listen_fd = socket_bindlisten(cliopt->socket_path, cliopt->socket_group);
  if (listen_fd < 0) {
    ERRORN("socket_bindlisten");
    goto done;
  }

  struct state state;
  memset(&state, 0, sizeof(state));
  state.sem = dispatch_semaphore_create(1);
  iface = start(&state, cliopt);
  if (iface == NULL) {
    // Error already logged.
    goto done;
  }

  if (pid_fd != -1) {
    char pid[20];
    snprintf(pid, sizeof(pid), "%u", getpid());
    if (write(pid_fd, pid, strlen(pid)) != (ssize_t)strlen(pid)) {
      ERRORN("write");
      goto done;
    }
  }

  while (1) {
    int accept_fd = accept(listen_fd, NULL, NULL);
    if (accept_fd < 0) {
      ERRORN("accept");
      goto done;
    }
    struct state *state_p = &state;
    dispatch_async(q, ^{
      on_accept(state_p, accept_fd, iface);
    });
  }
  rc = 0;
done:
  DEBUGF("shutting down with rc=%d", rc);
  dispatch_release(q);
  if (iface != NULL) {
    stop(iface);
  }
  if (listen >= 0) {
    close(listen_fd);
  }
  if (pid_fd != -1) {
    unlink(cliopt->pidfile);
    close(pid_fd);
  }
  cli_options_destroy(cliopt);
  return rc;
}

static void on_accept(struct state *state, int accept_fd, interface_ref iface) {
  INFOF("Accepted a connection (fd %d)", accept_fd);
  state_add_socket_fd(state, accept_fd);
  size_t buf_len = 64 * 1024;
  void *buf = malloc(buf_len);
  if (buf == NULL) {
    ERRORN("malloc");
    goto done;
  }
  for (uint64_t i = 0;; i++) {
    DEBUGF("[Socket-to-VMNET i=%lld] Receiving from the socket %d", i,
           accept_fd);
    uint32_t header_be = 0;
    ssize_t header_received = read(accept_fd, &header_be, 4);
    if (header_received < 0) {
      ERRORN("read[header]");
      goto done;
    }
    if (header_received == 0) {
      // EOF according to man page of read.
      INFOF("Connection closed by peer (fd %d)", accept_fd);
      goto done;
    }
    uint32_t header = ntohl(header_be);
    assert(header <= buf_len);
    ssize_t received = read(accept_fd, buf, header);
    if (received < 0) {
      ERRORN("read[body]");
      goto done;
    }
    if (received == 0) {
      // EOF according to man page of read.
      INFOF("Connection closed by peer (fd %d)", accept_fd);
      goto done;
    }
    assert(received == header);
    DEBUGF("[Socket-to-VMNET i=%lld] Received from the socket %d: %ld bytes", i,
           accept_fd, received);
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = header,
    };
    struct vmpktdesc pd = {
        .vm_pkt_size = header,
        .vm_pkt_iov = &iov,
        .vm_pkt_iovcnt = 1,
        .vm_flags = 0,
    };
    int written_count = pd.vm_pkt_iovcnt;
    DEBUGF("[Socket-to-VMNET i=%lld] Sending to VMNET: %ld bytes", i,
           pd.vm_pkt_size);
    vmnet_return_t write_status = vmnet_write(iface, &pd, &written_count);
    if (write_status != VMNET_SUCCESS) {
      ERRORF("vmnet_write: [%d] %s", write_status, vmnet_strerror(write_status));
      goto done;
    }
    DEBUGF("[Socket-to-VMNET i=%lld] Sent to VMNET: %ld bytes", i,
           pd.vm_pkt_size);

    // Flood the packet to other VMs in the same network too.
    // (Not handled by vmnet)
    // FIXME: avoid flooding
    dispatch_semaphore_wait(state->sem, DISPATCH_TIME_FOREVER);
    struct conn *conns = state->conns;
    dispatch_semaphore_signal(state->sem);
    for (struct conn *conn = conns; conn != NULL; conn = conn->next) {
      if (conn->socket_fd == accept_fd)
        continue;
      DEBUGF("[Socket-to-Socket i=%lld] Sending from socket %d to socket %d: "
             "4 + %d bytes",
             i, accept_fd, conn->socket_fd, header);
      struct iovec iov[2] = {
          {
              .iov_base = &header_be,
              .iov_len = 4,
          },
          {
              .iov_base = buf,
              .iov_len = header,
          },
      };
      ssize_t written = writev(conn->socket_fd, iov, 2);
      DEBUGF("[Socket-to-Socket i=%lld] Sent from socket %d to socket %d: %ld "
             "bytes (including uint32be header)",
             i, accept_fd, conn->socket_fd, written);
      if (written < 0) {
        ERRORN("writev");
        continue;
      }
    }
  }
done:
  INFOF("Closing a connection (fd %d)", accept_fd);
  state_remove_socket_fd(state, accept_fd);
  close(accept_fd);
  if (buf != NULL) {
    free(buf);
  }
}
