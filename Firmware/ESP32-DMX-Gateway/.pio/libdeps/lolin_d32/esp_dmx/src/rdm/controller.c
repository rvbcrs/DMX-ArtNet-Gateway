#include "controller.h"

#include "dmx/config.h"
#include "dmx/struct.h"
#include "endian.h"
#include "esp_dmx.h"
#include "rdm_utils.h"

bool rdm_send_disc_unique_branch(dmx_port_t dmx_num, rdm_header_t *header,
                                 const rdm_disc_unique_branch_t *branch,
                                 rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(branch != NULL, 0, "branch is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->dest_uid = RDM_UID_BROADCAST_ALL;
  header->port_id = dmx_num + 1;
  header->sub_device = RDM_SUB_DEVICE_ROOT;
  header->cc = RDM_CC_DISC_COMMAND;
  header->pid = RDM_PID_DISC_UNIQUE_BRANCH;
  header->pdl = sizeof(*branch);

  rdm_disc_unique_branch_t pd;
  size_t pdl = 0;
  rdm_pd_emplace(&pd, "uu$", branch, sizeof(pd), false);
  return rdm_send_request(dmx_num, header, &pd, NULL, &pdl, ack);
}

bool rdm_send_disc_mute(dmx_port_t dmx_num, rdm_header_t *header,
                        rdm_disc_mute_t *mute, rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->port_id = dmx_num + 1;
  header->sub_device = RDM_SUB_DEVICE_ROOT;
  header->cc = RDM_CC_DISC_COMMAND;
  header->pid = RDM_PID_DISC_MUTE;
  header->pdl = 0;

  rdm_disc_mute_t pd;
  size_t pdl = sizeof(pd);
  bool ret = rdm_send_request(dmx_num, header, NULL, &pd, &pdl, ack);
  if (ret && mute != NULL) {
    rdm_pd_emplace(mute, "wv$", &pd, sizeof(*mute), true);
  }

  return ret;
}

bool rdm_send_disc_un_mute(dmx_port_t dmx_num, rdm_header_t *header,
                           rdm_disc_mute_t *mute, rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->port_id = dmx_num + 1;
  header->sub_device = RDM_SUB_DEVICE_ROOT;
  header->cc = RDM_CC_DISC_COMMAND;
  header->pid = RDM_PID_DISC_UN_MUTE;
  header->pdl = 0;

  rdm_disc_mute_t pd;
  size_t pdl = sizeof(pd);
  bool ret = rdm_send_request(dmx_num, header, NULL, &pd, &pdl, ack);
  if (ret && mute != NULL) {
    rdm_pd_emplace(mute, "wv$", &pd, sizeof(*mute), true);
  }

  return ret;
}

int rdm_discover_with_callback(dmx_port_t dmx_num, rdm_disc_cb_t cb,
                               void *context) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(cb != NULL, 0, "cb is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  // Allocate the instruction stack. The max binary tree depth is 49
#ifdef CONFIG_RDM_STACK_ALLOCATE_DISCOVERY
  rdm_disc_unique_branch_t stack[49];  // 588 bytes - use with caution!
#else
  rdm_disc_unique_branch_t *stack;
  stack = malloc(sizeof(rdm_disc_unique_branch_t) * 49);
  DMX_CHECK(stack != NULL, 0, "discovery malloc error");
#endif

  // Initialize the stack with the initial branch instruction
  int stack_size = 1;
  stack[0].lower_bound = (rdm_uid_t){0, 0};
  stack[0].upper_bound = RDM_UID_MAX;

  rdm_header_t header;   // Send and receive header information.
  rdm_disc_mute_t mute;  // Mute parameters returned from devices.
  rdm_ack_t ack;         // Request response information.
  int num_found = 0;

  dmx_driver_t *const driver = dmx_driver[dmx_num];
  xSemaphoreTakeRecursive(driver->mux, 0);

  // Un-mute all devices
  header.dest_uid = RDM_UID_BROADCAST_ALL;
  rdm_send_disc_un_mute(dmx_num, &header, NULL, NULL);

  while (stack_size > 0) {
    // Pop a DISC_UNIQUE_BRANCH instruction parameter from the stack
    const rdm_disc_unique_branch_t *branch = &stack[--stack_size];

    size_t attempts = 0;
    if (rdm_uid_is_eq(&branch->lower_bound, &branch->upper_bound)) {
      // Can't branch further so attempt to mute the device
      header.dest_uid = branch->lower_bound;
      do {
        rdm_send_disc_mute(dmx_num, &header, &mute, &ack);
      } while (ack.type != RDM_RESPONSE_TYPE_ACK && ++attempts < 3);

      // TODO: remove this workaround?
      // Attempt to fix possible error where responder is flipping its own UID
      if (ack.type != RDM_RESPONSE_TYPE_ACK) {
        uint64_t uid = bswap64(((uint64_t)branch->lower_bound.man_id << 32) |
                               branch->lower_bound.dev_id) >>
                       16;
        header.dest_uid.man_id = uid >> 32;
        header.dest_uid.dev_id = uid;
        rdm_send_disc_mute(dmx_num, &header, &mute, &ack);
      }

      // Call the callback function and report a device has been found
      if (ack.type == RDM_RESPONSE_TYPE_ACK) {
        cb(dmx_num, ack.src_uid, num_found, &mute, context);
        ++num_found;
      }
    } else {
      // Search the current branch in the RDM address space
      do {
        rdm_send_disc_unique_branch(dmx_num, &header, branch, &ack);
      } while (ack.type == RDM_RESPONSE_TYPE_NONE && ++attempts < 3);
      if (ack.type != RDM_RESPONSE_TYPE_NONE) {
        bool devices_remaining = true;

#ifndef CONFIG_RDM_DEBUG_DEVICE_DISCOVERY
        /*
        Stop the RDM controller from branching all the way down to the
        individual address if it is not necessary. When debugging, this code
        should not be called as it can hide bugs in the discovery algorithm.
        Users can use the sdkconfig to enable or disable discovery debugging if
        it is desired, but it isn't necessary unless the user makes changes to
        this function.
        */
        if (ack.type == RDM_RESPONSE_TYPE_ACK) {
          do {
            // Attempt to mute the device
            attempts = 0;
            header.dest_uid = ack.src_uid;
            do {
              rdm_send_disc_mute(dmx_num, &header, &mute, &ack);
            } while (ack.type == RDM_RESPONSE_TYPE_NONE && ++attempts < 3);

            // Call the callback function and report a device has been found
            if (ack.type == RDM_RESPONSE_TYPE_ACK) {
              cb(dmx_num, ack.src_uid, num_found, &mute, context);
              ++num_found;
            }

            // Check if there are more devices in this branch
            attempts = 0;
            do {
              rdm_send_disc_unique_branch(dmx_num, &header, branch, &ack);
            } while (ack.type == RDM_RESPONSE_TYPE_NONE && ++attempts < 3);
          } while (ack.type == RDM_RESPONSE_TYPE_ACK);
          devices_remaining = (ack.err && ack.err != DMX_ERR_TIMEOUT);
        }
#endif

        // Iteratively search the next two RDM address spaces
        if (devices_remaining) {
          const rdm_uid_t first_lbound = branch->lower_bound;
          uint64_t mid = ((((uint64_t)branch->lower_bound.man_id << 32) |
                           branch->lower_bound.dev_id) +
                          (((uint64_t)branch->upper_bound.man_id << 32) |
                           branch->upper_bound.dev_id)) /
                         2;

          // Add the upper branch so that it gets handled second
          stack[stack_size].lower_bound.man_id = (mid + 1) >> 32;
          stack[stack_size].lower_bound.dev_id = mid + 1;
          // Reuse the upper_bound that is currently on the stack
          ++stack_size;

          // Add the lower branch so it gets handled first
          stack[stack_size].lower_bound = first_lbound;
          stack[stack_size].upper_bound.man_id = mid >> 32;
          stack[stack_size].upper_bound.dev_id = mid;

          ++stack_size;
        }
      }
    }
  }

  xSemaphoreGiveRecursive(driver->mux);

#ifndef RDM_STACK_ALLOCATE_DISCOVERY
  free(stack);
#endif

  return num_found;
}

struct rdm_disc_default_ctx {
  unsigned int num;
  rdm_uid_t *uids;
};

static void rdm_disc_cb(dmx_port_t dmx_num, rdm_uid_t uid, int num_found,
                        const rdm_disc_mute_t *mute, void *context) {
  struct rdm_disc_default_ctx *c = (struct rdm_disc_default_ctx *)context;
  if (num_found < c->num && c->uids != NULL) {
    c->uids[num_found] = uid;
  }
}

int rdm_discover_devices_simple(dmx_port_t dmx_num, rdm_uid_t *uids,
                                unsigned int num) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  struct rdm_disc_default_ctx context = {.num = num, .uids = uids};
  int found = rdm_discover_with_callback(dmx_num, &rdm_disc_cb, &context);

  return found;
}

bool rdm_send_get_device_info(dmx_port_t dmx_num, rdm_header_t *header,
                              rdm_device_info_t *device_info, rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(device_info != NULL, 0, "device_info is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->port_id = dmx_num + 1;
  header->cc = RDM_CC_GET_COMMAND;
  header->pid = RDM_PID_DEVICE_INFO;
  header->pdl = 0;

  rdm_device_info_t pd;
  size_t pdl = sizeof(pd);
  bool ret = rdm_send_request(dmx_num, header, NULL, &pd, &pdl, ack);
  if (ret) {
    rdm_pd_emplace(device_info, "#0100hwwdwbbwwb$", &pd, sizeof(*device_info),
                   true);
  }

  return ret;
}

bool rdm_send_get_software_version_label(dmx_port_t dmx_num,
                                         rdm_header_t *header,
                                         char *software_version_label,
                                         size_t *size, rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(software_version_label != NULL, 0,
            "software_version_label is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->port_id = dmx_num + 1;
  header->cc = RDM_CC_GET_COMMAND;
  header->pid = RDM_PID_SOFTWARE_VERSION_LABEL;
  header->pdl = 0;

  bool ret = rdm_send_request(dmx_num, header, NULL, software_version_label,
                              size, ack);
  if (ret) {
    software_version_label[*size] = '\0';
  }

  return ret;
}

bool rdm_send_get_identify_device(dmx_port_t dmx_num, rdm_header_t *header,
                                  uint8_t *identify, rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(identify != NULL, 0, "identify is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->port_id = dmx_num + 1;
  header->cc = RDM_CC_GET_COMMAND;
  header->pid = RDM_PID_IDENTIFY_DEVICE;
  header->pdl = 0;

  // Single-byte responses don't need to be emplaced
  size_t pdl = sizeof(*identify);
  return rdm_send_request(dmx_num, header, NULL, identify, &pdl, ack);
}

bool rdm_send_set_identify_device(dmx_port_t dmx_num, rdm_header_t *header,
                                  const uint8_t identify, rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(identify == 0 || identify == 1, 0, "identify is invalid");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->port_id = dmx_num + 1;
  header->cc = RDM_CC_SET_COMMAND;
  header->pid = RDM_PID_IDENTIFY_DEVICE;
  header->pdl = sizeof(identify);

  // Single-byte requests don't need to be emplaced
  size_t pdl = 0;
  return rdm_send_request(dmx_num, header, &identify, NULL, &pdl, ack);
}

bool rdm_send_get_dmx_start_address(dmx_port_t dmx_num, rdm_header_t *header,
                                    uint16_t *dmx_start_address,
                                    rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(dmx_start_address != NULL, 0, "dmx_start_address is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->port_id = dmx_num + 1;
  header->cc = RDM_CC_GET_COMMAND;
  header->pid = RDM_PID_DMX_START_ADDRESS;
  header->pdl = 0;

  uint16_t pd;
  size_t pdl = sizeof(pd);
  bool ret = rdm_send_request(dmx_num, header, NULL, &pd, &pdl, ack);
  if (ret) {
    rdm_pd_emplace(dmx_start_address, "w$", &pd, sizeof(*dmx_start_address),
                   true);
  }

  return ret;
}

bool rdm_send_set_dmx_start_address(dmx_port_t dmx_num, rdm_header_t *header,
                                    const uint16_t dmx_start_address,
                                    rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(dmx_start_address < 513, 0, "dmx_start_address is invalid");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  rdm_uid_get(dmx_num, &header->src_uid);
  header->port_id = dmx_num + 1;
  header->cc = RDM_CC_SET_COMMAND;
  header->pid = RDM_PID_DMX_START_ADDRESS;
  header->pdl = sizeof(dmx_start_address);

  uint16_t pd;
  size_t pdl = sizeof(pd);
  rdm_pd_emplace(&pd, "w$", &dmx_start_address, pdl, false);
  return rdm_send_request(dmx_num, header, &pd, NULL, &pdl, ack);
}
