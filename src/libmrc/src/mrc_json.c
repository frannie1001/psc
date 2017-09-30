
#include "mrc_json.h"

#include <assert.h>
#include <stdio.h>

int
mrc_json_get_integer(struct mrc_json_value *value)
{
  if (value->type == MRC_JSON_INTEGER) {
    return value->u.integer;
  } else {
    assert(0);
  }
}

double
mrc_json_get_double(struct mrc_json_value *value)
{
  if (value->type == MRC_JSON_DOUBLE) {
    return value->u.dbl;
  } else {
    assert(0);
  }
}

int
mrc_json_type(struct mrc_json_value *value)
{
  return value->type & MRC_JSON_TYPE_MASK;
}

// ======================================================================
// mrc_json_object (basic)

static unsigned int
mrc_json_object_basic_length(struct mrc_json_value *value)
{
  return value->u.obj.length;
}

static const char *
mrc_json_object_basic_entry_name(struct mrc_json_value *value, unsigned int i)
{
  assert(i < value->u.obj.length);
  return value->u.obj.entries[i].name;
}

static struct mrc_json_value *
mrc_json_object_basic_entry(struct mrc_json_value *value, unsigned int i)
{
  assert(i < value->u.obj.length);
  return value->u.obj.entries[i].value;
}

// ======================================================================
// mrc_json_object (class)

static unsigned int
mrc_json_object_class_length(struct mrc_json_value *value)
{
  int cnt;
  for (cnt = 0; value->u.cls.descr[cnt].name; cnt++)
    ;
  return cnt;
}

static const char *
mrc_json_object_class_entry_name(struct mrc_json_value *value, unsigned int i)
{
  struct mrc_json_class_entry *d = &value->u.cls.descr[i];
  return d->name;
}

static struct mrc_json_value *
mrc_json_object_class_entry(struct mrc_json_value *value, unsigned int i)
{
  struct mrc_json_class_entry *d = &value->u.cls.descr[i];
  void *base = (void *) value - value->u.cls.off;
  switch (d->value.type) {
  case MRC_JSON_INTEGER:
    d->value.u.integer = * (int *) (base + d->off);
    break;
  case MRC_JSON_DOUBLE:
    d->value.u.dbl = * (double *) (base + d->off);
    break;
  case MRC_JSON_REF_CLASS:
    return * (struct mrc_json_value **) (base + d->off);
  default:
    assert(0);
  }
  return &d->value;
}

// ======================================================================

static unsigned int mrc_json_object_mrc_obj_length(struct mrc_json_value *value);
static const char *mrc_json_object_mrc_obj_entry_name(struct mrc_json_value *value, unsigned int i);
static struct mrc_json_value *mrc_json_object_mrc_obj_entry(struct mrc_json_value *value, unsigned int i);

unsigned int
mrc_json_object_length(struct mrc_json_value *value)
{
  if (value->type == MRC_JSON_OBJECT) {
    return mrc_json_object_basic_length(value);
  } else if (value->type == MRC_JSON_OBJECT_CLASS) {
    return mrc_json_object_class_length(value);
  } else if (value->type == MRC_JSON_OBJECT_MRC_OBJ) {
    return mrc_json_object_mrc_obj_length(value);
  } else {
    assert(0);
  }
}

const char *
mrc_json_object_entry_name(struct mrc_json_value *value, unsigned int i)
{
  if (value->type == MRC_JSON_OBJECT) {
    return mrc_json_object_basic_entry_name(value, i);
  } else if (value->type == MRC_JSON_OBJECT_CLASS) {
    return mrc_json_object_class_entry_name(value, i);
  } else if (value->type == MRC_JSON_OBJECT_MRC_OBJ) {
    return mrc_json_object_mrc_obj_entry_name(value, i);
  } else {
    assert(0);
  }
}

struct mrc_json_value *
mrc_json_object_entry(struct mrc_json_value *value, unsigned int i)
{
  if (value->type == MRC_JSON_OBJECT) {
    return mrc_json_object_basic_entry(value, i);
  } else if (value->type == MRC_JSON_OBJECT_CLASS) {
    return mrc_json_object_class_entry(value, i);
  } else if (value->type == MRC_JSON_OBJECT_MRC_OBJ) {
    return mrc_json_object_mrc_obj_entry(value, i);
  } else {
    assert(0);
  }
}

// ======================================================================
// mrc_json_print

static void
print_indent(int depth)
{
  for (int j = 0; j < depth; j++) {
    printf(" ");
  }
}

static void
mrc_json_print_object(struct mrc_json_value* value, int depth)
{
  assert(value);

  print_indent(depth);
  printf("{\n");

  int length = mrc_json_object_length(value);
  for (int i = 0; i < length; i++) {
    print_indent(depth+2);
    printf("(name) %s : \n", mrc_json_object_entry_name(value, i));
    mrc_json_print(mrc_json_object_entry(value, i), depth+4);
  }

  print_indent(depth);
  printf("}\n");
}

void
mrc_json_print(struct mrc_json_value *value, int depth)
{
  assert(value);

  int type = mrc_json_type(value);
  switch (type) {
  case MRC_JSON_NONE:
    print_indent(depth);
    printf("(none)\n");
    break;
  case MRC_JSON_INTEGER:
    print_indent(depth);
    printf("(int) %d\n", mrc_json_get_integer(value));
    break;
  case MRC_JSON_DOUBLE:
    print_indent(depth);
    printf("(double) %g\n", mrc_json_get_double(value));
    break;
  case MRC_JSON_OBJECT:
    mrc_json_print_object(value, depth+1);
    break;
  default:
    fprintf(stderr, "MRC_JSON: unhandled type = %d\n", type);
    assert(0);
  }
}

// ======================================================================
// mrc_json_object (mrc_obj)

#include <mrc_obj.h>

static unsigned int
mrc_descr_length(struct param *params)
{
  if (!params) {
    return 0;
  }
  
  int cnt;
  for (cnt = 0; params[cnt].name; cnt++)
    ;

  return cnt;
}

static struct mrc_json_value *
mrc_descr_entry(struct param *param, char *p)
{
  // FIXME, this mrc_json_value is per-class, not per-object,
  // so if we look at multiple objects of the same class at the same
  // time, bugs will happen :(
  struct mrc_json_value *v = &param->json;
  
  switch (param->type) {
  case MRC_VAR_OBJ:
    v->type = MRC_JSON_NONE; // FIXME
    break;
  case PT_SELECT:
    v->type = MRC_JSON_NONE; // FIXME
    break;
  case PT_INT3:
    v->type = MRC_JSON_NONE; // FIXME
    break;
  default:
    fprintf(stderr, "unhandled type: %d\n", param->type);
    assert(0);
  }

  return v;
}

static unsigned int
mrc_json_object_mrc_obj_length(struct mrc_json_value *value)
{
  struct mrc_obj *obj = container_of(value, struct mrc_obj, json);
  struct mrc_class *cls = obj->cls;

  int cnt = 2; // mrc_obj type and name

  cnt += mrc_descr_length(cls->param_descr);

  if (obj->ops) {
    cnt += mrc_descr_length(obj->ops->param_descr);
  }
  
  printf("length = %d\n", cnt);
  return cnt;
}

static const char *
mrc_json_object_mrc_obj_entry_name(struct mrc_json_value *value, unsigned int i)
{
  struct mrc_obj *obj = container_of(value, struct mrc_obj, json);
  struct mrc_class *cls = obj->cls;

  if (i == 0) {
    return "mrc_obj_type";
  }
  i--;

  if (i == 0) {
    return "mrc_obj_name";
  }
  i--;
  
  int len = mrc_descr_length(cls->param_descr);
  if (i < len) {
    return cls->param_descr[i].name;
  }
  i -= len;

  if (obj->ops) {
    int len = mrc_descr_length(obj->ops->param_descr);
    if (i < len) {
      return obj->ops->param_descr[i].name;
    }
    i-= len;
  }

  assert(0);
}

static struct mrc_json_value *
mrc_json_object_mrc_obj_entry(struct mrc_json_value *value, unsigned int i)
{
  struct mrc_obj *obj = container_of(value, struct mrc_obj, json);
  struct mrc_class *cls = obj->cls;

  if (i == 0) {
    static struct mrc_json_value v_type;
    v_type.type = MRC_JSON_NONE;
    return &v_type;
  }
  i--;

  if (i == 0) {
    static struct mrc_json_value v_name;
    v_name.type = MRC_JSON_NONE;
    return &v_name;
  }
  i--;
  
  int len = mrc_descr_length(cls->param_descr);
  if (i < len) {
    return mrc_descr_entry(&cls->param_descr[i], (char *) obj + cls->param_offset);
  }
  i -= len;

  if (obj->ops) {
    int len = mrc_descr_length(obj->ops->param_descr);
    if (i < len) {
      return mrc_descr_entry(&obj->ops->param_descr[i], (char *) obj->subctx + obj->ops->param_offset);
    }
    i-= len;
  }

  assert(0);

}
