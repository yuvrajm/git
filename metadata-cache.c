#include "cache.h"
#include "metadata-cache.h"
#include "sha1-lookup.h"
#include "object.h"
#include "commit.h"

static struct metadata_cache **autowrite;
static int autowrite_nr;
static int autowrite_alloc;

static int installed_atexit_autowriter;

static int record_size(const struct metadata_cache *c)
{
	/* a record is a 20-byte sha1 plus the width of the value */
	return c->mem.width + 20;
}

static const char *metadata_cache_path(const char *name)
{
	return git_path("cache/%s", name);
}

static void close_disk_cache(struct metadata_cache *c)
{
	if (c->map) {
		munmap(c->map, c->maplen);
		c->map = NULL;
		c->maplen = 0;
		c->disk_entries = 0;
		c->disk_nr = 0;
	}

	if (c->fd >= 0) {
		close(c->fd);
		c->fd = -1;
	}
}

static unsigned char *check_cache_header(struct metadata_cache *c,
					 const char *path)
{
	unsigned char *p = c->map;
	unsigned char validity[20];
	uint32_t version;
	uint32_t width;

	if (c->maplen < 32) {
		warning("cache file '%s' is short (%lu bytes)",
			path, c->maplen);
		return NULL;
	}

	if (memcmp(p, "MTAC", 4)) {
		warning("cache file '%s' has invalid magic: %c%c%c%c",
			path, p[0], p[1], p[2], p[3]);
		return NULL;
	}
	p += 4;

	version = ntohl(*(uint32_t *)p);
	if (version != 1) {
		warning("cache file '%s' has unknown version: %"PRIu32,
			path, version);
		return NULL;
	}
	p += 4;

	width = ntohl(*(uint32_t *)p);
	if (width != c->mem.width) {
		warning("cache file '%s' does not have desired width: "
			"(%"PRIu32" != %u", path, width, c->mem.width);
		return NULL;
	}
	p += 4;

	if (c->validity_fun) {
		c->validity_fun(validity);
		if (hashcmp(validity, p))
			return NULL;
	}
	else {
		if (!is_null_sha1(p))
			return NULL;
	}
	p += 20;

	return p;
}

static void open_disk_cache(struct metadata_cache *c, const char *path)
{
	struct stat sb;

	c->fd = open(path, O_RDONLY);
	if (c->fd < 0)
		return;

	if (fstat(c->fd, &sb) < 0) {
		close_disk_cache(c);
		return;
	}

	c->maplen = sb.st_size;
	c->map = xmmap(NULL, c->maplen, PROT_READ, MAP_PRIVATE, c->fd, 0);

	c->disk_entries = check_cache_header(c, path);
	if (!c->disk_entries) {
		close_disk_cache(c);
		return;
	}
	c->disk_nr = (sb.st_size - (c->disk_entries - c->map)) / record_size(c);
}

static unsigned char *flatten_mem_entries(struct metadata_cache *c)
{
	int i;
	unsigned char *ret;
	int nr;

	ret = xmalloc(c->mem.nr * record_size(c));
	nr = 0;
	for (i = 0; i < c->mem.size; i++) {
		struct object_decoration *e = decoration_slot(&c->mem, i);
		unsigned char *out;

		if (!e->base)
			continue;

		if (nr == c->mem.nr)
			die("BUG: decorate hash contained extra values");

		out = ret + (nr * record_size(c));
		hashcpy(out, e->base->sha1);
		out += 20;
		memcpy(out, e->decoration, c->mem.width);
		nr++;
	}

	return ret;
}

static int void_hashcmp(const void *a, const void *b)
{
	return hashcmp(a, b);
}

static int write_header(int fd, struct metadata_cache *c)
{
	uint32_t width;
	unsigned char validity[20];

	if (write_in_full(fd, "MTAC\x00\x00\x00\x01", 8) < 0)
		return -1;

	width = htonl(c->mem.width);
	if (write_in_full(fd, &width, 4) < 0)
		return -1;

	if (c->validity_fun)
		c->validity_fun(validity);
	else
		hashcpy(validity, null_sha1);
	if (write_in_full(fd, validity, 20) < 0)
		return -1;

	return 0;
}

static int merge_entries(int fd, int size,
			 const unsigned char *left, unsigned nr_left,
			 const unsigned char *right, unsigned nr_right)
{
#define ADVANCE(name) \
	do { \
		name += size; \
		nr_##name--; \
	} while(0)
#define WRITE_ENTRY(name) \
	do { \
		if (write_in_full(fd, name, size) < 0) \
			return -1; \
		ADVANCE(name); \
	} while(0)

	while (nr_left && nr_right) {
		int cmp = hashcmp(left, right);

		/* skip duplicates, preferring left to right */
		if (cmp == 0)
			ADVANCE(right);
		else if (cmp < 0)
			WRITE_ENTRY(left);
		else
			WRITE_ENTRY(right);
	}
	while (nr_left)
		WRITE_ENTRY(left);
	while (nr_right)
		WRITE_ENTRY(right);

#undef WRITE_ENTRY
#undef ADVANCE

	return 0;
}

static int metadata_cache_write(struct metadata_cache *c, const char *name)
{
	const char *path = metadata_cache_path(name);
	struct strbuf tempfile = STRBUF_INIT;
	int fd;
	unsigned char *mem_entries;

	if (!c->mem.nr)
		return 0;

	strbuf_addf(&tempfile, "%s.XXXXXX", path);
	if (safe_create_leading_directories(tempfile.buf) < 0 ||
	    (fd = git_mkstemp_mode(tempfile.buf, 0755)) < 0) {
		strbuf_release(&tempfile);
		return -1;
	}

	if (write_header(fd, c) < 0)
		goto fail;

	mem_entries = flatten_mem_entries(c);
	qsort(mem_entries, c->mem.nr, record_size(c), void_hashcmp);

	if (merge_entries(fd, record_size(c),
			  mem_entries, c->mem.nr,
			  c->disk_entries, c->disk_nr) < 0) {
		free(mem_entries);
		goto fail;
	}
	free(mem_entries);

	if (close(fd) < 0)
		goto fail;
	if (rename(tempfile.buf, path) < 0)
		goto fail;

	strbuf_release(&tempfile);
	return 0;

fail:
	close(fd);
	unlink(tempfile.buf);
	strbuf_release(&tempfile);
	return -1;
}
static void autowrite_metadata_caches(void)
{
	int i;
	for (i = 0; i < autowrite_nr; i++)
		metadata_cache_write(autowrite[i], autowrite[i]->mem.name);
}

static void metadata_cache_init(struct metadata_cache *c)
{
	if (c->initialized)
		return;

	open_disk_cache(c, metadata_cache_path(c->mem.name));

	ALLOC_GROW(autowrite, autowrite_nr+1, autowrite_alloc);
	autowrite[autowrite_nr++] = c;
	if (!installed_atexit_autowriter) {
		atexit(autowrite_metadata_caches);
		installed_atexit_autowriter = 1;
	}

	c->initialized = 1;
}

static void *lookup_disk(struct metadata_cache *c,
			 const struct object *obj)
{
	int pos;

	pos = sha1_entry_pos(c->disk_entries, record_size(c), 0,
			     0, c->disk_nr, c->disk_nr, obj->sha1);
	if (pos < 0)
		return NULL;

	return c->disk_entries + (pos * record_size(c)) + 20;
}

const void *metadata_cache_lookup(struct metadata_cache *c,
				  const struct object *obj)
{
	void *r;

	metadata_cache_init(c);

	r = lookup_decoration_value(&c->mem, obj);
	if (!r)
		r = lookup_disk(c, obj);
	return r;
}

void metadata_cache_add(struct metadata_cache *c, const struct object *obj,
			const void *value)
{
	metadata_cache_init(c);
	add_decoration_value(&c->mem, obj, value, NULL);
}

int metadata_cache_lookup_uint32(struct metadata_cache *c,
				 const struct object *obj,
				 uint32_t *value)
{
	const uint32_t *out;

	if (record_size(c) != 24)
		die("BUG: size mismatch in object cache lookup (%d != 24)",
		    record_size(c));

	out = metadata_cache_lookup(c, obj);
	if (!out)
		return -1;

	*value = ntohl(*out);
	return 0;
}

void metadata_cache_add_uint32(struct metadata_cache *c,
			       const struct object *obj,
			       uint32_t value)
{
	if (record_size(c) != 24)
		die("BUG: size mismatch in object cache add (%d != 24)",
		    record_size(c));

	value = htonl(value);
	metadata_cache_add(c, obj, &value);
}

void metadata_graph_validity(unsigned char out[20])
{
	git_SHA_CTX ctx;

	git_SHA1_Init(&ctx);

	git_SHA1_Update(&ctx, "grafts", 6);
	commit_graft_validity(&ctx);

	git_SHA1_Update(&ctx, "replace", 7);
	replace_object_validity(&ctx);

	git_SHA1_Final(out, &ctx);
}
