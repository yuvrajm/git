/*
 * decorate.c - decorate a git object with some arbitrary
 * data.
 */
#include "cache.h"
#include "object.h"
#include "decorate.h"

static unsigned int hash_obj(const struct object *obj, unsigned int n)
{
	unsigned int hash;

	memcpy(&hash, obj->sha1, sizeof(unsigned int));
	return hash % n;
}

static int insert_decoration(struct decoration *n, const struct object *base,
			     const void *decoration, void *old)
{
	int size = n->size;
	unsigned long width = decoration_width(n);
	unsigned int j = hash_obj(base, size);

	while (1) {
		struct object_decoration *e = decoration_slot(n, j);
		if (!e->base) {
			e->base = base;
			memcpy(e->decoration, decoration, width);
			n->nr++;
			return 0;
		}
		if (e->base == base) {
			if (old)
				memcpy(old, e->decoration, width);
			memcpy(e->decoration, decoration, width);
			return 1;
		}
		if (++j >= size)
			j = 0;
	}
}

static void grow_decoration(struct decoration *n)
{
	int i;
	int old_size = n->size;
	unsigned char *old_hash = n->hash;

	n->size = (old_size + 1000) * 3 / 2;
	n->hash = xcalloc(n->size, decoration_stride(n));
	n->nr = 0;

	for (i = 0; i < old_size; i++) {
		struct object_decoration *e =
			(struct object_decoration *)
			(old_hash + i * decoration_stride(n));
		if (e->base)
			insert_decoration(n, e->base, e->decoration, NULL);
	}
	free(old_hash);
}

/* Add a decoration pointer, return any old one */
void *add_decoration(struct decoration *n, const struct object *obj,
		void *decoration)
{
	void *old = NULL;
	add_decoration_value(n, obj, &decoration, &old);
	return old;
}

int add_decoration_value(struct decoration *n,
			 const struct object *obj,
			 const void *decoration,
			 void *old)
{
	int nr = n->nr + 1;
	if (nr > n->size * 2 / 3)
		grow_decoration(n);
	return insert_decoration(n, obj, decoration, old);
}

/* Lookup a decoration pointer */
void *lookup_decoration(const struct decoration *n, const struct object *obj)
{
	void **v;

	v = lookup_decoration_value(n, obj);
	if (!v)
		return NULL;
	return *v;
}

void *lookup_decoration_value(const struct decoration *n,
			      const struct object *obj)
{
	unsigned int j;

	/* nothing to lookup */
	if (!n->size)
		return NULL;
	j = hash_obj(obj, n->size);
	for (;;) {
		struct object_decoration *ref = decoration_slot(n, j);
		if (ref->base == obj)
			return ref->decoration;
		if (!ref->base)
			return NULL;
		if (++j == n->size)
			j = 0;
	}
}
