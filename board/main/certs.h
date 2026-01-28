#pragma once

#include <stdint.h>
#include <stddef.h>

extern const uint8_t _binary_rbots_root_ca_pem_start[];
extern const uint8_t _binary_rbots_root_ca_pem_end[];

#define RBOTS_ROOT_CA_PEM_START ((const char *)_binary_rbots_root_ca_pem_start)
#define RBOTS_ROOT_CA_PEM_LEN ((size_t)(_binary_rbots_root_ca_pem_end - _binary_rbots_root_ca_pem_start))
