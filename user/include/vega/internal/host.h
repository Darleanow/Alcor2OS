/**
 * @file vega/internal/host.h
 * @brief libvega-internal access to the registered host ops. Do not include
 * from outside libvega — use <vega/host.h> + vega_init() instead.
 */

#ifndef VEGA_INTERNAL_HOST_H
#define VEGA_INTERNAL_HOST_H

#include <vega/host.h>

extern const vega_host_ops_t *vega_host;

#endif /* VEGA_INTERNAL_HOST_H */
