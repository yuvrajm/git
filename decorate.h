#ifndef DECORATE_H
#define DECORATE_H

struct object_decoration {
	const struct object *base;
	unsigned char decoration[FLEX_ARRAY];
};

struct decoration {
	const char *name;
	/* width of data we're holding; must be set before adding */
	const unsigned int width;
	unsigned int size, nr;
	/*
	 * The hash contains object_decoration structs, but we don't know their
	 * size until runtime. So we store is as a pointer to characters to
	 * make pointer arithmetic easier.
	 */
	unsigned char *hash;
};

extern void *add_decoration(struct decoration *n, const struct object *obj, void *decoration);
extern void *lookup_decoration(const struct decoration *n, const struct object *obj);

extern int add_decoration_value(struct decoration *n,
				const struct object *obj,
				const void *decoration,
				void *old);
extern void *lookup_decoration_value(const struct decoration *n,
				     const struct object *obj);

static inline unsigned long decoration_width(const struct decoration *n)
{
	return n->width ? n->width : sizeof(void *);
}

static inline unsigned long decoration_stride(const struct decoration *n)
{
	return sizeof(struct object_decoration) + decoration_width(n);
}

static inline struct object_decoration *decoration_slot(const struct decoration *n,
							unsigned i)
{
	return (struct object_decoration *)
		(n->hash + (i * decoration_stride(n)));
}

#endif
