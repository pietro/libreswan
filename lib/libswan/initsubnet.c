/*
 * initialize subnet structure
 * Copyright (C) 2000, 2002  Henry Spencer.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/lgpl-2.1.txt>.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
 * License for more details.
 */

#include "ip_subnet.h"
#include "ip_info.h" 	/* ipv6_info */
#include "lswlog.h"	/* for dbg() */

/*
 * addrtosubnet - initialize ip_subnet from an address:port
 *
 * XXX: yes, address:port; not address
 *
 * The [old] code copied END directly into .addr and because that was
 * a sockaddr underneath it would include the port.  This means that
 * code creating the client's subnet from the end's .host_addr is
 * (intentional or otherwise) creating a subnet for address:port.  It
 * might help explain why code keeps stuffing the client's port into
 * .host_addr.
 *
 * NULL for success, else string literal
 */

static ip_subnet subnet3(const ip_address *address, int maskbits, int port)
{
	ip_endpoint e = endpoint(address, port);
	ip_subnet s = {
		.addr = e,
		.maskbits = maskbits,
		.is_subnet = true,
	};
	psubnet(&s);
	return s;
}

static ip_subnet subnet_from_endpoint(const ip_endpoint *endpoint)
{
	const struct ip_info *afi = endpoint_type(endpoint);
	if (!pexpect(afi != NULL)) {
		return unset_subnet;
	}
	ip_address address = endpoint_address(endpoint);
	int hport = endpoint_hport(endpoint);
	pexpect(hport != 0);
	return subnet3(&address, afi->mask_cnt, hport);
}

err_t endtosubnet(const ip_endpoint *endpoint, ip_subnet *dst, where_t where)
{
	const struct ip_info *afi = endpoint_type(endpoint);
	if (afi == NULL) {
		/* actually AF_UNSPEC */
		*dst = unset_subnet;
		return "unknown address family";
	}

	ip_subnet s;
	if (endpoint_hport(endpoint) == 0) {
		endpoint_buf eb_;
		dbg("subnet from address %s "PRI_WHERE,
		    str_endpoint(endpoint, &eb_),
		    pri_where(where));
		ip_address a = endpoint_address(endpoint);
		s = subnet_from_address(&a);
	} else {
		endpoint_buf eb_;
		dbg("subnet from endpoint %s "PRI_WHERE,
		    str_endpoint(endpoint, &eb_),
		    pri_where(where));
		s = subnet_from_endpoint(endpoint);
	}
	*dst = s;
	return NULL;
}
