#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <vmnet/vmnet.h>
#include <libvdeplug.h>

static bool debug = false;

#define DEBUGF(fmt, ...) \
            do { if (debug) fprintf(stderr, "DEBUG| "fmt "\n", __VA_ARGS__); } while (0)

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
  if(param == NULL) return;
  xpc_dictionary_apply(param, ^bool(const char *key, xpc_object_t value) {
    xpc_type_t t = xpc_get_type(value);
    if (t == XPC_TYPE_UINT64)  printf("* %s: %lld\n", key, xpc_uint64_get_value(value));
    else if (t == XPC_TYPE_INT64)  printf("* %s: %lld\n", key, xpc_int64_get_value(value));
    else if (t == XPC_TYPE_STRING)  printf("* %s: %s\n", key, xpc_string_get_string_ptr(value));
    else if (t == XPC_TYPE_UUID){
      char uuid_str[36 + 1];
      uuid_unparse(xpc_uuid_get_bytes(value), uuid_str);
      printf("* %s: %s\n", key, uuid_str);
    } else printf("* %s: (unknown type)\n", key);
    return true;
  });
}

static void on_vmnet_packets_available(uint64_t event_id, interface_ref iface, int64_t estim_count, int64_t max_bytes, VDECONN *vdeconn) {
	DEBUGF("[Event %lld] Receiving from VMNET (estimated %lld packets, max: %lld bytes)", event_id, estim_count, max_bytes);

	size_t buf_len = max_bytes;
	void *buf = malloc(buf_len); // TODO: prealloc
	if (buf == NULL ) {
		perror("malloc");
		goto done;
	}

	struct iovec iov = {
		.iov_base = buf,
		.iov_len = buf_len,
	};
	struct vmpktdesc pd = {
		.vm_pkt_size = buf_len,
		.vm_pkt_iov = &iov,
		.vm_pkt_iovcnt = 1,
		.vm_flags = 0,
	};
	int received_count = pd.vm_pkt_iovcnt;
	vmnet_return_t read_status = vmnet_read(iface, &pd, &received_count);
  print_vmnet_status(__FUNCTION__, read_status);
  if (read_status != VMNET_SUCCESS) {
      perror("vmnet_read");
      goto done;
  }

	DEBUGF("[Event %lld] Received from VMNET: %ld bytes", event_id, pd.vm_pkt_size);
	DEBUGF("[Event %lld] Sending to VDE: %ld bytes", event_id, pd.vm_pkt_size);
  ssize_t written = vde_send(vdeconn, buf, pd.vm_pkt_size, 0);
	DEBUGF("[Event %lld] Sent to VDE: %ld bytes", event_id, written);
  if (written != (ssize_t)pd.vm_pkt_size) {
      perror("vde_send");
      goto done;
  }
done:
	if (buf != NULL) {
		free(buf);
	}
}

static interface_ref start(VDECONN *vdeconn) {
  printf("Initializing vmnet.framework (VMNET_SHARED_MODE, with random interface ID)\n");
  xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
  // TODO: support non-shared modes
  xpc_dictionary_set_uint64(dict, vmnet_operation_mode_key, VMNET_SHARED_MODE);

  uuid_t uuid;
  // TODO: support deterministic UUID and MAC address
  uuid_generate_random(uuid);
  xpc_dictionary_set_uuid(dict, vmnet_interface_id_key, uuid);

  dispatch_queue_t q = dispatch_queue_create("com.example.vde_vmnet.start", DISPATCH_QUEUE_SERIAL);
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);

  __block interface_ref iface;
  __block vmnet_return_t status;

  __block uint64_t max_bytes = 0;
  iface = vmnet_start_interface(dict, q,
    ^(vmnet_return_t x_status, xpc_object_t x_param){
      status = x_status;
      if (x_status == VMNET_SUCCESS) {
        print_vmnet_start_param(x_param);
        max_bytes = xpc_dictionary_get_uint64(x_param, vmnet_max_packet_size_key);
      }
      dispatch_semaphore_signal(sem);
    }
  );
  dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
  print_vmnet_status(__FUNCTION__, status);
  dispatch_release(q);
  xpc_release(dict);
  if (status != VMNET_SUCCESS) {
    return NULL;
  }

  dispatch_queue_t event_q = dispatch_queue_create("com.example.vde_vmnet.events", DISPATCH_QUEUE_SERIAL);
	vmnet_interface_set_event_callback(iface, VMNET_INTERFACE_PACKETS_AVAILABLE, event_q,
    ^(interface_event_t x_event_id, xpc_object_t x_event) {
			uint64_t estim_count = xpc_dictionary_get_uint64(x_event, vmnet_estimated_packets_available_key);
			on_vmnet_packets_available(x_event_id, iface, estim_count, max_bytes, vdeconn);
		}
	);

  return iface;
}

static void stop(interface_ref iface) {
  if (iface == NULL) {
    return;
  }
  dispatch_queue_t q = dispatch_queue_create("com.example.vde_vmnet.stop", DISPATCH_QUEUE_SERIAL);
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  __block vmnet_return_t status;
  vmnet_stop_interface(iface, q,
    ^(vmnet_return_t x_status) {
      status = x_status;
      dispatch_semaphore_signal(sem);
    }
  );
  dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
  print_vmnet_status(__FUNCTION__, status);
  dispatch_release(q);
	// TODO: release event_q ?
}

int main(int argc, char *argv[]){
  debug = getenv("DEBUG") != NULL;
  int rc = 1;
  VDECONN *vdeconn = NULL;
  char *vdeswitch = NULL; // don't free
  __block interface_ref iface= NULL;
  void *buf = NULL;
  if (argc != 2) {
    fprintf(stderr, "Usage: %s VDESWITCH\n", argv[0]);
    return 1;
  }
  if (geteuid() != 0) {
    fprintf(stderr, "WARNING: Running without root. This is very unlikely to work. See README.md .\n");
  }
  vdeswitch = argv[1];
  struct vde_open_args vdeargs = {
    .port = 0, // VDE switch port number, not TCP port number
    .group = "staff", // TODO: this shouldn't be hard-coded ?
    .mode = 0770,
  };
  DEBUGF("Opening VDE \"%s\" (for UNIX group \"%s\")", vdeswitch, vdeargs.group);
  vdeconn = vde_open(vdeswitch, "vde_vmnet", &vdeargs);
  if (vdeconn == NULL) {
    perror("vde_open");
    goto done;
  }

  iface = start(vdeconn);
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
  for (int i=0; ;i++){
    DEBUGF("[i=%d] Receiving from VDE", i);
    ssize_t received = vde_recv(vdeconn, buf, buf_len, 0);
    if (received < 0) {
      perror("vde_recv");
      goto done;
    }
    DEBUGF("[i=%d] Received from VDE: %ld bytes", i, received);
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
    DEBUGF("[i=%d] Sending to VMNET: %ld bytes", i, pd.vm_pkt_size);
    vmnet_return_t write_status = vmnet_write(iface, &pd, &written_count);
    print_vmnet_status(__FUNCTION__, write_status);
    if (write_status != VMNET_SUCCESS) {
      perror("vmnet_write");
      goto done;
    }
    DEBUGF("[i=%d] Sent to VMNET: %ld bytes", i, pd.vm_pkt_size);
  }
  rc = 0;
done:
  DEBUGF("shutting down with rc=%d", rc);
  if (iface!= NULL) {
    stop(iface);
  }
  if (vdeconn != NULL) {
    vde_close(vdeconn);
  }
  if (buf != NULL) {
    free(buf);
  }
  return rc;
}
