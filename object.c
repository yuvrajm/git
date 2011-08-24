#include "cache.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"

static const char *object_type_strings[] = {
	NULL,		/* OBJ_NONE = 0 */
	"commit",	/* OBJ_COMMIT = 1 */
	"tree",		/* OBJ_TREE = 2 */
	"blob",		/* OBJ_BLOB = 3 */
	"tag",		/* OBJ_TAG = 4 */
};

const char *typename(unsigned int type)
{
	if (type >= ARRAY_SIZE(object_type_strings))
		return NULL;
	return object_type_strings[type];
}

int type_from_string(const char *str)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(object_type_strings); i++)
		if (!strcmp(str, object_type_strings[i]))
			return i;
	die("invalid object type \"%s\"", str);
}

static struct object **obj_hash;
static int nr_objs, obj_hash_size;

unsigned int get_max_object_index(void)
{
	return obj_hash_size;
}

struct object *get_indexed_object(unsigned int idx)
{
	return obj_hash[idx];
}


/* Choose from 2, 3, 4 or 5 */
#define CUCKOO_FACTOR 4

#define H(hv,ix) ((hv[ix]) & (obj_hash_size-1))

struct object *lookup_object(const unsigned char *sha1)
{
	struct object *obj;
	const unsigned int *hashval;

	if (!obj_hash)
		return NULL;

	hashval = (const unsigned int *)sha1;
	if ((obj = obj_hash[H(hashval, 0)]) && !hashcmp(sha1, obj->sha1))
		return obj;
	if ((obj = obj_hash[H(hashval, 1)]) && !hashcmp(sha1, obj->sha1))
		return obj;
#if CUCKOO_FACTOR >= 3
	if ((obj = obj_hash[H(hashval, 2)]) && !hashcmp(sha1, obj->sha1))
		return obj;
#endif
#if CUCKOO_FACTOR >= 4
	if ((obj = obj_hash[H(hashval, 3)]) && !hashcmp(sha1, obj->sha1))
		return obj;
#endif
#if CUCKOO_FACTOR >= 5
	if ((obj = obj_hash[H(hashval, 4)]) && !hashcmp(sha1, obj->sha1))
		return obj;
#endif
	return NULL;
}

static void grow_object_hash(void); /* forward */

/*
 * A naive single-table cuckoo hashing implementation.
 * Return NULL when "obj" is successfully inserted. Otherwise
 * return a pointer to the object to be inserted (which may
 * be different from the original obj). The caller is expected
 * to grow the hash table and re-insert the returned object.
 */
static struct object *insert_obj_hash(struct object *obj)
{
	int loop;

	for (loop = obj_hash_size - obj_hash_size / 8; 0 <= loop; loop--) {
		struct object *tmp_obj;
		unsigned int ix, i0;
		const unsigned int *hashval;

		hashval = (const unsigned int *)(obj->sha1);
		i0 = ix = H(hashval, 0);
		tmp_obj = obj_hash[i0];
		if (!tmp_obj) {
			obj_hash[ix] = obj;
			return NULL;
		}
		ix = H(hashval, 1);
		if (!obj_hash[ix]) {
			obj_hash[ix] = obj;
			return NULL;
		}
#if CUCKOO_FACTOR >= 3
		ix = H(hashval, 2);
		if (!obj_hash[ix]) {
			obj_hash[ix] = obj;
			return NULL;
		}
#endif
#if CUCKOO_FACTOR >= 4
		ix = H(hashval, 3);
		if (!obj_hash[ix]) {
			obj_hash[ix] = obj;
			return NULL;
		}
#endif
#if CUCKOO_FACTOR >= 5
		ix = H(hashval, 4);
		if (!obj_hash[ix]) {
			obj_hash[ix] = obj;
			return NULL;
		}
#endif
		obj_hash[i0] = obj;
		obj = tmp_obj;
	}
	return obj;
}

static int next_size(int sz)
{
	return (sz < 32 ? 32 :
		(sz < 1024 * 1024 ? 8 : 2) * sz);
}

static void grow_object_hash(void)
{
	struct object **current_hash;
	int current_size;

	current_hash = obj_hash;
	current_size = obj_hash_size;
	while (1) {
		int i;
		obj_hash_size = next_size(obj_hash_size);
		obj_hash = xcalloc(obj_hash_size, sizeof(struct object *));

		for (i = 0; i < current_size; i++) {
			if (!current_hash[i])
				continue;
			if (insert_obj_hash(current_hash[i]))
				break;
		}
		if (i < current_size) {
			/* too small - grow and retry */
			free(obj_hash);
			continue;
		}
		free(current_hash);
		return;
	}
}

void *create_object(const unsigned char *sha1, int type, void *o)
{
	struct object *obj = o;
	struct object *to_insert;

	obj->parsed = 0;
	obj->used = 0;
	obj->type = type;
	obj->flags = 0;
	hashcpy(obj->sha1, sha1);

	if (!obj_hash_size)
		grow_object_hash();

	to_insert = obj;
	while (1) {
		to_insert = insert_obj_hash(to_insert);
		if (!to_insert)
			break;
		grow_object_hash();
	}
	nr_objs++;
	return obj;
}

struct object *lookup_unknown_object(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj)
		obj = create_object(sha1, OBJ_NONE, alloc_object_node());
	return obj;
}

struct object *parse_object_buffer(const unsigned char *sha1, enum object_type type, unsigned long size, void *buffer, int *eaten_p)
{
	struct object *obj;
	int eaten = 0;

	obj = NULL;
	if (type == OBJ_BLOB) {
		struct blob *blob = lookup_blob(sha1);
		if (blob) {
			if (parse_blob_buffer(blob, buffer, size))
				return NULL;
			obj = &blob->object;
		}
	} else if (type == OBJ_TREE) {
		struct tree *tree = lookup_tree(sha1);
		if (tree) {
			obj = &tree->object;
			if (!tree->object.parsed) {
				if (parse_tree_buffer(tree, buffer, size))
					return NULL;
				eaten = 1;
			}
		}
	} else if (type == OBJ_COMMIT) {
		struct commit *commit = lookup_commit(sha1);
		if (commit) {
			if (parse_commit_buffer(commit, buffer, size))
				return NULL;
			if (!commit->buffer) {
				commit->buffer = buffer;
				eaten = 1;
			}
			obj = &commit->object;
		}
	} else if (type == OBJ_TAG) {
		struct tag *tag = lookup_tag(sha1);
		if (tag) {
			if (parse_tag_buffer(tag, buffer, size))
			       return NULL;
			obj = &tag->object;
		}
	} else {
		warning("object %s has unknown type id %d\n", sha1_to_hex(sha1), type);
		obj = NULL;
	}
	if (obj && obj->type == OBJ_NONE)
		obj->type = type;
	*eaten_p = eaten;
	return obj;
}

struct object *parse_object(const unsigned char *sha1)
{
	unsigned long size;
	enum object_type type;
	int eaten;
	const unsigned char *repl = lookup_replace_object(sha1);
	void *buffer = read_sha1_file(sha1, &type, &size);

	if (buffer) {
		struct object *obj;
		if (check_sha1_signature(repl, buffer, size, typename(type)) < 0) {
			free(buffer);
			error("sha1 mismatch %s\n", sha1_to_hex(repl));
			return NULL;
		}

		obj = parse_object_buffer(sha1, type, size, buffer, &eaten);
		if (!eaten)
			free(buffer);
		return obj;
	}
	return NULL;
}

struct object_list *object_list_insert(struct object *item,
				       struct object_list **list_p)
{
	struct object_list *new_list = xmalloc(sizeof(struct object_list));
	new_list->item = item;
	new_list->next = *list_p;
	*list_p = new_list;
	return new_list;
}

int object_list_contains(struct object_list *list, struct object *obj)
{
	while (list) {
		if (list->item == obj)
			return 1;
		list = list->next;
	}
	return 0;
}

void add_object_array(struct object *obj, const char *name, struct object_array *array)
{
	add_object_array_with_mode(obj, name, array, S_IFINVALID);
}

void add_object_array_with_mode(struct object *obj, const char *name, struct object_array *array, unsigned mode)
{
	unsigned nr = array->nr;
	unsigned alloc = array->alloc;
	struct object_array_entry *objects = array->objects;

	if (nr >= alloc) {
		alloc = (alloc + 32) * 2;
		objects = xrealloc(objects, alloc * sizeof(*objects));
		array->alloc = alloc;
		array->objects = objects;
	}
	objects[nr].item = obj;
	objects[nr].name = name;
	objects[nr].mode = mode;
	array->nr = ++nr;
}

void object_array_remove_duplicates(struct object_array *array)
{
	unsigned int ref, src, dst;
	struct object_array_entry *objects = array->objects;

	for (ref = 0; ref + 1 < array->nr; ref++) {
		for (src = ref + 1, dst = src;
		     src < array->nr;
		     src++) {
			if (!strcmp(objects[ref].name, objects[src].name))
				continue;
			if (src != dst)
				objects[dst] = objects[src];
			dst++;
		}
		array->nr = dst;
	}
}
