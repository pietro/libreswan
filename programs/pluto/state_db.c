/* State table indexed by serialno, for libreswan
 *
 * Copyright (C) 2015-2019 Andrew Cagney <cagney@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "defs.h"

#include "state_db.h"
#include "state.h"
#include "connections.h"
#include "lswlog.h"
#include "hash_table.h"

static struct hash_table state_hashes[];

static void jam_state(struct lswlog *buf, const void *data)
{
	if (data == NULL) {
		jam(buf, "#0");
	} else {
		const struct state *st = data;
		jam(buf, "#%lu", st->st_serialno);
	}
}

static bool state_plausable(struct state *st,
			    enum ike_version ike_version,
			    const so_serial_t *clonedfrom,
			    const msgid_t *v1_msgid,
			    const enum sa_role *role)
{
	if (st->st_ike_version != ike_version) {
		return false;
	}
	if (v1_msgid != NULL && st->st_msgid != *v1_msgid) {
		return false;
	}
	if (role != NULL && st->st_sa_role != *role) {
		return false;
	}
	if (clonedfrom != NULL && st->st_clonedfrom != *clonedfrom) {
		return false;
	}
	return true;
}

/*
 * A table ordered by serialno.
 */

static const struct list_info serialno_list_info = {
	.name = "serialno list",
	.jam = jam_state,
};

struct list_head serialno_list_head;

/*
 * A table hashed by serialno.
 */

static hash_t serialno_hasher(const so_serial_t *serialno)
{
	return hasher(shunk2(serialno, sizeof(*serialno)), zero_hash);
}

static hash_t serialno_state_hasher(const void *data)
{
	const struct state *st = data;
	return serialno_hasher(&st->st_serialno);
}

static struct list_entry *serialno_state_entry(void *data)
{
	struct state *st = data;
	return &st->st_hash_entries[SERIALNO_STATE_HASH];
}

static struct list_head serialno_hash_slots[STATE_TABLE_SIZE];

struct state *state_by_serialno(so_serial_t serialno)
{
	/*
	 * Note that since SOS_NOBODY is never hashed, a lookup of
	 * SOS_NOBODY always returns NULL.
	 */
	struct state *st;
	hash_t hash = serialno_hasher(&serialno);
	struct list_head *bucket = hash_table_bucket(&state_hashes[SERIALNO_STATE_HASH], hash);
	FOR_EACH_LIST_ENTRY_NEW2OLD(bucket, st) {
		if (st->st_serialno == serialno) {
			return st;
		}
	}
	return NULL;
}

struct ike_sa *ike_sa_by_serialno(so_serial_t serialno)
{
	return pexpect_ike_sa(state_by_serialno(serialno));
}

struct child_sa *child_sa_by_serialno(so_serial_t serialno)
{
	return pexpect_child_sa(state_by_serialno(serialno));
}

/*
 * A table hashed by the connection's address.
 */

static hash_t connection_hasher(struct connection *const *connection)
{
	return hasher(shunk2(connection, sizeof(*connection)), zero_hash);
}

static hash_t connection_state_hasher(const void *data)
{
	const struct state *st = data;
	return connection_hasher(&st->st_connection);
}

static struct list_entry *connection_state_entry(void *data)
{
	struct state *st = data;
	return &st->st_hash_entries[CONNECTION_STATE_HASH];
}

static struct list_head connection_hash_slots[STATE_TABLE_SIZE];

struct state *state_by_connection(struct connection *connection,
				  state_by_predicate *predicate /*optional*/,
				  void *predicate_context,
				  const char *reason)
{
	/*
	 * Note that since SOS_NOBODY is never hashed, a lookup of
	 * SOS_NOBODY always returns NULL.
	 */
	struct state *st;
	hash_t hash = connection_hasher(&connection);
	struct list_head *bucket = hash_table_bucket(&state_hashes[CONNECTION_STATE_HASH], hash);
	FOR_EACH_LIST_ENTRY_NEW2OLD(bucket, st) {
		if (st->st_connection != connection) {
			continue;
		}
		if (predicate != NULL &&
		    !predicate(st, predicate_context)) {
			continue;
		}
		dbg("State DB: found state #%lu in %s (%s)",
		    st->st_serialno, st->st_state->short_name, reason);
		return st;
	}
	dbg("State DB: state not found (%s)", reason);
	return NULL;
}

void rehash_state_connection(struct state *st)
{
	rehash_table_entry(&state_hashes[CONNECTION_STATE_HASH], st);
}

/*
 * A table hashed by reqid.
 */

static hash_t reqid_hasher(const reqid_t *reqid)
{
	return hasher(shunk2(reqid, sizeof(*reqid)), zero_hash);
}

static hash_t reqid_state_hasher(const void *data)
{
	const struct state *st = data;
	return reqid_hasher(&st->st_reqid);
}

static struct list_entry *reqid_state_entry(void *data)
{
	struct state *st = data;
	return &st->st_hash_entries[REQID_STATE_HASH];
}

static struct list_head reqid_hash_slots[STATE_TABLE_SIZE];

struct state *state_by_reqid(reqid_t reqid,
			     state_by_predicate *predicate /*optional*/,
			     void *predicate_context,
			     const char *reason)
{
	/*
	 * Note that since SOS_NOBODY is never hashed, a lookup of
	 * SOS_NOBODY always returns NULL.
	 */
	struct state *st;
	hash_t hash = reqid_hasher(&reqid);
	struct list_head *bucket = hash_table_bucket(&state_hashes[REQID_STATE_HASH], hash);
	FOR_EACH_LIST_ENTRY_NEW2OLD(bucket, st) {
		if (st->st_reqid != reqid) {
			continue;
		}
		if (predicate != NULL &&
		    !predicate(st, predicate_context)) {
			continue;
		}
		dbg("State DB: found state #%lu in %s (%s)",
		    st->st_serialno, st->st_state->short_name, reason);
		return st;
	}
	dbg("State DB: state not found (%s)", reason);
	return NULL;
}

void rehash_state_reqid(struct state *st)
{
	rehash_table_entry(&state_hashes[REQID_STATE_HASH], st);
}

/*
 * Hash table indexed by just the IKE SPIi.
 *
 * The response to an IKE_SA_INIT contains an as yet unknown SPIr
 * value.  Hence, when looking for the initiator of an IKE_SA_INIT
 * response, only the SPIi key is used.
 *
 * When a CHILD SA is emancipated creating a new IKE SA its IKE SPIs
 * are replaced, hence a rehash is required.
 */

static hash_t ike_initiator_spi_hasher(const ike_spi_t *ike_initiator_spi)
{
	return hasher(shunk2(ike_initiator_spi, sizeof(*ike_initiator_spi)), zero_hash);
}

static hash_t ike_initiator_spi_state_hasher(const void *data)
{
	const struct state *st = data;
	return ike_initiator_spi_hasher(&st->st_ike_spis.initiator);
}

static struct list_entry *ike_initiator_spi_state_entry(void *data)
{
	struct state *st = data;
	return &st->st_hash_entries[IKE_INITIATOR_SPI_STATE_HASH];
}

static void jam_ike_initiator_spi(struct lswlog *buf, const void *data)
{
	const struct state *st = data;
	jam_state(buf, st);
	jam(buf, ": ");
	jam_dump_bytes(buf, st->st_ike_spis.initiator.bytes,
		       sizeof(st->st_ike_spis.initiator.bytes));
}

static struct list_head ike_initiator_spi_hash_slots[STATE_TABLE_SIZE];

struct state *state_by_ike_initiator_spi(enum ike_version ike_version,
					 const so_serial_t *clonedfrom, /*optional*/
					 const msgid_t *v1_msgid, /*optional*/
					 const enum sa_role *role, /*optional*/
					 const ike_spi_t *ike_initiator_spi,
					 const char *name)
{
	hash_t hash = ike_initiator_spi_hasher(ike_initiator_spi);
	struct list_head *bucket = hash_table_bucket(&state_hashes[IKE_INITIATOR_SPI_STATE_HASH], hash);
	struct state *st = NULL;
	FOR_EACH_LIST_ENTRY_NEW2OLD(bucket, st) {
		if (!state_plausable(st, ike_version, clonedfrom, v1_msgid, role)) {
			continue;
		}
		if (!ike_spi_eq(&st->st_ike_spis.initiator, ike_initiator_spi)) {
			continue;
		}
		dbg("State DB: found %s state #%lu in %s (%s)",
		    enum_name(&ike_version_names, ike_version),
		    st->st_serialno, st->st_state->short_name, name);
		return st;
	}
	dbg("State DB: %s state not found (%s)",
	    enum_name(&ike_version_names, ike_version), name);
	return NULL;
}

/*
 * Hash table indexed by both both IKE SPIi+SPIr.
 *
 * Note that these values change over time and when this happens a
 * rehash is required:
 *
 * - initially SPIr=0, but then when the first response is received
 *   SPIr changes to the value in that response
 *
 * - when a CHILD SA is emancipated creating a new IKE SA, the IKE
 *   SPIs of the child change to those from the CREATE_CHILD_SA
 *   exchange
 */

static hash_t ike_spis_hasher(const ike_spis_t *ike_spis)
{
	return hasher(shunk2(ike_spis, sizeof(*ike_spis)), zero_hash);
}

static hash_t ike_spis_state_hasher(const void *data)
{
	const struct state *st = data;
	return ike_spis_hasher(&st->st_ike_spis);
}

static struct list_entry *ike_spis_state_entry(void *data)
{
	struct state *st = data;
	return &st->st_hash_entries[IKE_SPIS_STATE_HASH];
}

static void jam_ike_spis(struct lswlog *buf, const void *data)
{
	const struct state *st = data;
	jam_state(buf, st);
	jam(buf, ": ");
	jam_dump_bytes(buf, st->st_ike_spis.initiator.bytes,
		       sizeof(st->st_ike_spis.initiator.bytes));
	jam(buf, "  ");
	jam_dump_bytes(buf, st->st_ike_spis.responder.bytes,
		       sizeof(st->st_ike_spis.responder.bytes));
}

static struct list_head ike_spis_hash_slots[STATE_TABLE_SIZE];

struct state *state_by_ike_spis(enum ike_version ike_version,
				const so_serial_t *clonedfrom,
				const msgid_t *v1_msgid, /*optional*/
				const enum sa_role *sa_role, /*optional*/
				const ike_spis_t *ike_spis,
				state_by_predicate *predicate,
				void *predicate_context,
				const char *name)
{
	hash_t hash = ike_spis_hasher(ike_spis);
	struct list_head *bucket = hash_table_bucket(&state_hashes[IKE_SPIS_STATE_HASH], hash);
	struct state *st = NULL;
	FOR_EACH_LIST_ENTRY_NEW2OLD(bucket, st) {
		if (!state_plausable(st, ike_version, clonedfrom, v1_msgid, sa_role)) {
			continue;
		}
		if (!ike_spis_eq(&st->st_ike_spis, ike_spis)) {
			continue;
		}
		if (predicate != NULL) {
			if (!predicate(st, predicate_context)) {
				continue;
			}
		}
		dbg("State DB: found %s state #%lu in %s (%s)",
		    enum_name(&ike_version_names, ike_version),
		    st->st_serialno, st->st_state->short_name, name);
		return st;
	}
	dbg("State DB: %s state not found (%s)",
	    enum_name(&ike_version_names, ike_version), name);
	return NULL;
}

/*
 * Maintain the contents of the hash tables.
 *
 * Unlike serialno, the IKE SPI[ir] keys can change over time.
 */
static struct hash_table state_hashes[STATE_HASH_ROOF] = {
	[SERIALNO_STATE_HASH] = {
		.info = {
			.name = "st_serialno table",
			.jam = jam_state,
		},
		.hasher = serialno_state_hasher,
		.entry = serialno_state_entry,
		.nr_slots = STATE_TABLE_SIZE,
		.slots = serialno_hash_slots,
	},
	[CONNECTION_STATE_HASH] = {
		.info = {
			.name = "st_connection table",
			.jam = jam_state,
		},
		.hasher = connection_state_hasher,
		.entry = connection_state_entry,
		.nr_slots = STATE_TABLE_SIZE,
		.slots = connection_hash_slots,
	},
	[REQID_STATE_HASH] = {
		.info = {
			.name = "st_reqid table",
			.jam = jam_state,
		},
		.hasher = reqid_state_hasher,
		.entry = reqid_state_entry,
		.nr_slots = STATE_TABLE_SIZE,
		.slots = reqid_hash_slots,
	},
	[IKE_INITIATOR_SPI_STATE_HASH] = {
		.info = {
			.name = "IKE SPIi table",
			.jam = jam_ike_initiator_spi,
		},
		.hasher = ike_initiator_spi_state_hasher,
		.entry = ike_initiator_spi_state_entry,
		.nr_slots = STATE_TABLE_SIZE,
		.slots = ike_initiator_spi_hash_slots,
	},
	[IKE_SPIS_STATE_HASH] = {
		.info = {
			.name = "IKE SPI[ir] table",
			.jam = jam_ike_spis,
		},
		.hasher = ike_spis_state_hasher,
		.entry = ike_spis_state_entry,
		.nr_slots = STATE_TABLE_SIZE,
		.slots = ike_spis_hash_slots,
	},
};

void add_state_to_db(struct state *st)
{
	dbg("State DB: adding %s state #%lu in %s",
	    enum_name(&ike_version_names, st->st_ike_version),
	    st->st_serialno, st->st_state->short_name);
	passert(st->st_serialno != SOS_NOBODY);

	/* serial NR list, entries are only added */
	st->st_serialno_list_entry = list_entry(&serialno_list_info, st);
	insert_list_entry(&serialno_list_head,
			  &st->st_serialno_list_entry);

	for (unsigned h = 0; h < elemsof(state_hashes); h++) {
		add_hash_table_entry(&state_hashes[h], st);
	}
}

void rehash_state_cookies_in_db(struct state *st)
{
	dbg("State DB: re-hashing %s state #%lu IKE SPIi and SPI[ir]",
	    enum_name(&ike_version_names, st->st_ike_version),
	    st->st_serialno);

	rehash_table_entry(&state_hashes[IKE_SPIS_STATE_HASH], st);
	rehash_table_entry(&state_hashes[IKE_INITIATOR_SPI_STATE_HASH], st);
}

void del_state_from_db(struct state *st)
{
	dbg("State DB: deleting %s state #%lu in %s",
	    enum_name(&ike_version_names, st->st_ike_version),
	    st->st_serialno, st->st_state->short_name);
	remove_list_entry(&st->st_serialno_list_entry);
	for (unsigned h = 0; h < elemsof(state_hashes); h++) {
		del_hash_table_entry(&state_hashes[h], st);
	}
}

void init_state_db(void)
{
	init_list(&serialno_list_info, &serialno_list_head);
	for (unsigned h = 0; h < elemsof(state_hashes); h++) {
		init_hash_table(&state_hashes[h]);
	}
}
