#ifndef METADATA_CACHE_H
#define METADATA_CACHE_H

#include "decorate.h"

typedef void (*metadata_cache_validity_fun)(unsigned char out[20]);

struct metadata_cache {
	metadata_cache_validity_fun validity_fun;

	/* in memory entries */
	struct decoration mem;

	/* mmap'd disk entries */
	int fd;
	unsigned char *map;
	unsigned long maplen;
	unsigned char *disk_entries;
	int disk_nr;

	int initialized;
};

#define METADATA_CACHE_INIT(name, width, validity) \
	{ validity, { (name), (width) } }

/* Convenience wrappers around metadata_cache_{lookup,add} */
int metadata_cache_lookup_uint32(struct metadata_cache *,
				 const struct object *,
				 uint32_t *value);
void metadata_cache_add_uint32(struct metadata_cache *,
			       const struct object *,
			       uint32_t value);

/* Common validity token functions */
void metadata_graph_validity(unsigned char out[20]);

#endif /* METADATA_CACHE_H */
