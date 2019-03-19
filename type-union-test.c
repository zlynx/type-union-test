#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// log10(2^64) + 2
#define MAX_INTSTRING_LEN 21

enum VAL_types {
  VAL_UNDEFINED,
  VAL_INT32,
  VAL_STRING,
  VAL_OBJECT,
};

enum OPS_RESULT_errors {
  OPS_RESULT_OK,
  OPS_RESULT_FALSE,
  OPS_RESULT_UNIMPLEMENTED,
  OPS_RESULT_INVALID_TYPE,
  OPS_RESULT_INVALID_INTEGER,
};

struct VAL;
struct OBJECT;

union VAL_type_data {
  int32_t int32;
  char *string;
  struct OBJECT *object;
};

typedef struct OPS_RESULT {
  struct VAL *val;
  enum OPS_RESULT_errors error;
} OPS_RESULT;

typedef struct VAL_OPS {
  OPS_RESULT (*set_type)(struct VAL *, enum VAL_types);
  OPS_RESULT (*copy_from_int32)(struct VAL *, int32_t);
  OPS_RESULT (*copy_from_string)(struct VAL *, const char *);
  OPS_RESULT (*move_from_key_val)(struct VAL *, struct VAL *, struct VAL *);
  OPS_RESULT (*is_equal)(struct VAL *, struct VAL *);
  OPS_RESULT (*debug_print)(struct VAL *);
} VAL_OPS;

typedef struct VAL {
  enum VAL_types type_id;
  size_t ref_count;
  union VAL_type_data type_data;
  const VAL_OPS *ops;
  bool constant;
  bool owned_ptr;
} VAL;

typedef struct OBJECT_KV {
  VAL *key;
  VAL *val;
} OBJECT_KV;

typedef struct OBJECT {
  OBJECT_KV *array;
  size_t len;
  size_t cap;
} OBJECT;

OBJECT *OBJECT_new(void);
void OBJECT_delete(OBJECT *op);
VAL *VAL_new(void);
void VAL_delete(VAL *vp);

bool result_ok(OPS_RESULT res) { return res.error == OPS_RESULT_OK; }

const char *result_error_str(enum OPS_RESULT_errors err) {
  switch (err) {
  case OPS_RESULT_OK:
    return "OK";
  case OPS_RESULT_FALSE:
    return "false";
  case OPS_RESULT_UNIMPLEMENTED:
    return "unimplemented";
  case OPS_RESULT_INVALID_TYPE:
    return "invalid type";
  case OPS_RESULT_INVALID_INTEGER:
    return "invalid integer";
  default:
    return "unknown error";
  }
}

void result_print(OPS_RESULT res) {
  FILE *out = stdout;
  fprintf(out, "{error: \"%s\"", result_error_str(res.error));
  if (result_ok(res) && res.val) {
    res.val->ops->debug_print(res.val);
  }
  fprintf(out, "}");
}

VAL *result_unwrap(OPS_RESULT res) {
  if (res.error != OPS_RESULT_OK) {
    result_print(res);
    printf("\n");
    fflush(stdout);
    abort();
  }
  return res.val;
}

void *xmalloc(size_t bytes) {
  void *p = malloc(bytes);
  if (!p)
    abort();
  return p;
}

void xfree(void *p) { free(p); }

void xrealloc(void **p, size_t bytes) {
  void *new_p = realloc(*p, bytes);
  if (!new_p)
    abort();
  *p = new_p;
}

// Got to take into account the virtual functions we are not using yet!
// One val may have reimplemented is_equal so check both ways. For SCIENCE!
// And unnecessary complexity!
OPS_RESULT VAL_is_equal(VAL *v1_p, VAL *v2_p) {
  if (result_ok(v1_p->ops->is_equal(v1_p, v2_p)) &&
      result_ok(v2_p->ops->is_equal(v2_p, v1_p)))
    return (OPS_RESULT){.error = OPS_RESULT_OK};
  return (OPS_RESULT){.error = OPS_RESULT_FALSE};
}

OPS_RESULT VAL_default_set_type(VAL *vp, enum VAL_types type_id) {
  if (vp->type_id != VAL_UNDEFINED && vp->type_id != type_id)
    // Would need to implement type conversion.
    return (OPS_RESULT){.error = OPS_RESULT_UNIMPLEMENTED};
  vp->type_id = type_id;
  switch (type_id) {
  case VAL_OBJECT:
    vp->type_data.object = OBJECT_new();
    break;
  default:
    // Do nothing special.
    break;
  }
  return (OPS_RESULT){.error = OPS_RESULT_OK};
}

OPS_RESULT VAL_default_copy_from_int32(VAL *vp, int32_t source) {
  int r;
  switch (vp->type_id) {
  case VAL_INT32:
    vp->type_data.int32 = source;
    break;
  case VAL_STRING:
    if (vp->type_data.string)
      xfree(vp->type_data.string);
    vp->type_data.string = xmalloc(MAX_INTSTRING_LEN);
    r = snprintf(vp->type_data.string, MAX_INTSTRING_LEN, "%d", source);
    if (r >= MAX_INTSTRING_LEN)
      abort();
    break;
  default:
    return (OPS_RESULT){.error = OPS_RESULT_INVALID_TYPE};
  }
  return (OPS_RESULT){.error = OPS_RESULT_OK};
}

OPS_RESULT VAL_default_copy_from_string(VAL *vp, const char *s) {
  int r;
  char *cp;
  long lval;
  switch (vp->type_id) {
  case VAL_INT32:
    errno = 0;
    lval = strtol(s, &cp, 0);
    if (errno == ERANGE || !(*cp == '\0' || isspace(*cp)) ||
        !(lval <= INT_MAX && lval >= INT_MIN))
      return (OPS_RESULT){.error = OPS_RESULT_INVALID_INTEGER};
    vp->type_data.int32 = lval;
    break;
  case VAL_STRING:
    if (vp->type_data.string)
      xfree(vp->type_data.string);
    r = strlen(s);
    vp->type_data.string = xmalloc(r + 1);
    strcpy(vp->type_data.string, s);
    break;
  default:
    return (OPS_RESULT){.error = OPS_RESULT_INVALID_TYPE};
  }
  return (OPS_RESULT){.error = OPS_RESULT_OK};
}

// This is a move because it does not increment reference counts of key or val.
OPS_RESULT VAL_default_move_from_key_val(VAL *vp, VAL *key, VAL *val) {
  // Must be an OBJECT
  if (vp->type_id != VAL_OBJECT)
    return (OPS_RESULT){.error = OPS_RESULT_INVALID_TYPE};
  // Find existing key
  size_t i;
  for (i = 0; i < vp->type_data.object->len; i++) {
    if (result_ok(VAL_is_equal(vp->type_data.object->array[i].key, key))) {
      // Delete existing key and value
      VAL_delete(vp->type_data.object->array[i].key);
      VAL_delete(vp->type_data.object->array[i].val);
      break;
    }
  }
  // Insert new key and value
  if (i == vp->type_data.object->len) {
    // Might have to realloc.
    if (i == vp->type_data.object->cap) {
      if (vp->type_data.object->cap > 0)
        vp->type_data.object->cap *= 2;
      else
        vp->type_data.object->cap = 4;
      xrealloc((void **)&vp->type_data.object->array,
               vp->type_data.object->cap * sizeof *vp->type_data.object->array);
    }
    vp->type_data.object->len++;
  }
  vp->type_data.object->array[i].key = key;
  vp->type_data.object->array[i].val = val;
  return (OPS_RESULT){.error = OPS_RESULT_OK};
}

OPS_RESULT VAL_default_is_equal(VAL *v1_p, VAL *v2_p) {
  // Not going to do type conversion right now.
  if (v1_p->type_id != v2_p->type_id)
    return (OPS_RESULT){.error = OPS_RESULT_UNIMPLEMENTED};
  switch (v1_p->type_id) {
  case VAL_INT32:
    if (v1_p->type_data.int32 != v2_p->type_data.int32)
      return (OPS_RESULT){.error = OPS_RESULT_FALSE};
    break;
  case VAL_STRING:
    if (strcmp(v1_p->type_data.string, v2_p->type_data.string) != 0)
      return (OPS_RESULT){.error = OPS_RESULT_FALSE};
    break;
  default:
    // Not going to compare OBJECTS right now. Too hard.
    return (OPS_RESULT){.error = OPS_RESULT_UNIMPLEMENTED};
  }
  return (OPS_RESULT){.error = OPS_RESULT_OK};
}

OPS_RESULT VAL_default_debug_print(VAL *vp) {
  FILE *out = stdout;
  size_t i;
  switch (vp->type_id) {
  case VAL_INT32:
    fprintf(out, "%d", vp->type_data.int32);
    break;
  case VAL_STRING:
    fprintf(out, "\"%s\"", vp->type_data.string);
    break;
  case VAL_OBJECT:
    fprintf(out, "{");
    for (i = 0; i < vp->type_data.object->len; i++) {
      if (i > 0)
        fprintf(out, ", ");
      vp->type_data.object->array[i].key->ops->debug_print(
          vp->type_data.object->array[i].key);
      fprintf(out, ": ");
      vp->type_data.object->array[i].val->ops->debug_print(
          vp->type_data.object->array[i].val);
    }
    fprintf(out, "}");
    break;
  default:
    fprintf(out, "\"undefined type\"");
    break;
  }
  return (OPS_RESULT){.error = OPS_RESULT_OK};
}

static const VAL_OPS VAL_OPS_template = {
    .set_type = VAL_default_set_type,
    .copy_from_int32 = VAL_default_copy_from_int32,
    .copy_from_string = VAL_default_copy_from_string,
    .move_from_key_val = VAL_default_move_from_key_val,
    .is_equal = VAL_default_is_equal,
    .debug_print = VAL_default_debug_print,
};

static const VAL VAL_template = {.type_id = VAL_UNDEFINED,
                                 .ref_count = 1,
                                 .type_data = {0},
                                 .ops = &VAL_OPS_template,
                                 .constant = false,
                                 .owned_ptr = false};

VAL *VAL_new(void) {
  VAL *p = xmalloc(sizeof *p);
  *p = VAL_template;
  return p;
}

void VAL_delete(VAL *vp) {
  if (--vp->ref_count == 0) {
    switch (vp->type_id) {
    case VAL_STRING:
      xfree(vp->type_data.string);
      break;
    case VAL_OBJECT:
      OBJECT_delete(vp->type_data.object);
      break;
    default:
      // Do nothing.
      break;
    }
    xfree(vp);
  }
}

static const OBJECT OBJECT_template = {0};

OBJECT *OBJECT_new(void) {
  OBJECT *p = xmalloc(sizeof *p);
  *p = OBJECT_template;
  return p;
}

void OBJECT_delete(OBJECT *op) {
  for (size_t i = 0; i < op->len; i++) {
    VAL_delete(op->array[i].key);
    VAL_delete(op->array[i].val);
  }
  xfree(op->array);
  xfree(op);
}

int main(int argc, char *argv[]) {
  VAL *top = VAL_new();
  result_unwrap(top->ops->set_type(top, VAL_OBJECT));
  for (int i = 1; i < argc - 1; i += 2) {
    VAL *key = VAL_new();
    VAL *val = VAL_new();
    result_unwrap(key->ops->set_type(key, VAL_INT32));
    // key->ops->copy_from_int32(key, i);
    result_unwrap(key->ops->copy_from_string(key, argv[i]));
    result_unwrap(val->ops->set_type(val, VAL_STRING));
    // val->ops->copy_from_string(val, argv[i]);
    result_unwrap(val->ops->copy_from_string(val, argv[i + 1]));

    result_unwrap(top->ops->move_from_key_val(top, key, val));
  }

  top->ops->debug_print(top);
  printf("\n");

  VAL_delete(top);
  return 0;
}
