/**
 * collectd - src/target_set.c
 * Copyright (C) 2008       Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "common.h"
#include "filter_chain.h"

struct ts_key_list_s
{
  char *key;
  struct ts_key_list_s *next;
};
typedef struct ts_key_list_s ts_key_list_t;

static void ts_key_list_free (ts_key_list_t *l) /* {{{ */
{
  if (l == NULL)
    return;

  free (l->key);

  if (l->next != NULL)
    ts_key_list_free (l->next);

  free (l);
} /* }}} void ts_name_list_free */

struct ts_data_s
{
  char *host;
  char *plugin;
  char *plugin_instance;
  /* char *type; */
  char *type_instance;
  meta_data_t *meta;
  ts_key_list_t *meta_delete;
};
typedef struct ts_data_s ts_data_t;

static int ts_util_get_key_and_string_wo_strdup (const oconfig_item_t *ci, char **ret_key, char **ret_string) /* {{{ */
{
  if ((ci->values_num != 2)
      || (ci->values[0].type != OCONFIG_TYPE_STRING)
      || (ci->values[1].type != OCONFIG_TYPE_STRING))
  {
    ERROR ("ts_util_get_key_and_string_wo_strdup: The %s option requires "
        "exactly two string arguments.", ci->key);
    return (-1);
  }

  *ret_key = ci->values[0].value.string;
  *ret_string = ci->values[1].value.string;

  return (0);
} /* }}} int ts_util_get_key_and_string_wo_strdup */

static int ts_config_add_string (char **dest, /* {{{ */
    const oconfig_item_t *ci, int may_be_empty)
{
  char *tmp = NULL;
  int status;

  status = cf_util_get_string (ci, &tmp);
  if (status != 0)
    return (status);

  if (!may_be_empty && (strlen (tmp) == 0))
  {
    ERROR ("Target `set': The `%s' option does not accept empty strings.",
        ci->key);
    sfree (tmp);
    return (-1);
  }

  *dest = tmp;
  return (0);
} /* }}} int ts_config_add_string */

static int ts_config_add_meta (meta_data_t **dest, /* {{{ */
    const oconfig_item_t *ci, int may_be_empty)
{
  char *key = NULL;
  char *string = NULL;
  int status;

  status = ts_util_get_key_and_string_wo_strdup (ci, &key, &string);
  if (status != 0)
    return (status);

  if (strlen (key) == 0)
  {
    ERROR ("Target `set': The `%s' option does not accept empty string as "
        "first argument.", ci->key);
    return (-1);
  }

  if (!may_be_empty && (strlen (string) == 0))
  {
    ERROR ("Target `set': The `%s' option does not accept empty string as "
        "second argument.", ci->key);
    return (-1);
  }

  if ((*dest) == NULL)
  {
    /* Create a new meta_data_t */
    if ((*dest = meta_data_create()) == NULL)
    {
      ERROR ("Target `set': failed to create a meta data for `%s'.", ci->key);
      return (-ENOMEM);
    }
  }

  return (meta_data_add_string (*dest, key, string));
} /* }}} int ts_config_add_meta */

static int ts_config_add_meta_delete (ts_key_list_t **dest, /* {{{ */
    const oconfig_item_t *ci)
{
  ts_key_list_t *entry = NULL;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    ERROR ("ts_config_add_meta_delete: The %s option requires "
        "exactly one string argument.", ci->key);
    return (-1);
  }

  if (strlen (ci->values[0].value.string) == 0)
  {
    ERROR ("Target `set': The `%s' option does not accept empty string as "
        "first argument.", ci->key);
    return (-1);
  }

  entry = calloc (1, sizeof (*entry));
  if (entry == NULL)
  {
    ERROR ("ts_config_add_meta_delete: calloc failed.");
    return (-ENOMEM);
  }

  entry->key = sstrdup (ci->values[0].value.string);
  entry->next = *dest;
  *dest = entry;

  return (0);
} /* }}} int ts_config_add_meta_delete */

static int ts_destroy (void **user_data) /* {{{ */
{
  ts_data_t *data;

  if (user_data == NULL)
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
    return (0);

  free (data->host);
  free (data->plugin);
  free (data->plugin_instance);
  /* free (data->type); */
  free (data->type_instance);
  meta_data_destroy(data->meta);
  ts_key_list_free (data->meta_delete);
  free (data);

  return (0);
} /* }}} int ts_destroy */

static int ts_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  ts_data_t *data;
  int status;

  data = calloc (1, sizeof (*data));
  if (data == NULL)
  {
    ERROR ("ts_create: calloc failed.");
    return (-ENOMEM);
  }

  data->host = NULL;
  data->plugin = NULL;
  data->plugin_instance = NULL;
  /* data->type = NULL; */
  data->type_instance = NULL;
  data->meta = NULL;
  data->meta_delete = NULL;

  status = 0;
  for (int i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if ((strcasecmp ("Host", child->key) == 0)
        || (strcasecmp ("Hostname", child->key) == 0))
      status = ts_config_add_string (&data->host, child,
          /* may be empty = */ 0);
    else if (strcasecmp ("Plugin", child->key) == 0)
      status = ts_config_add_string (&data->plugin, child,
          /* may be empty = */ 0);
    else if (strcasecmp ("PluginInstance", child->key) == 0)
      status = ts_config_add_string (&data->plugin_instance, child,
          /* may be empty = */ 1);
#if 0
    else if (strcasecmp ("Type", child->key) == 0)
      status = ts_config_add_string (&data->type, child,
          /* may be empty = */ 0);
#endif
    else if (strcasecmp ("TypeInstance", child->key) == 0)
      status = ts_config_add_string (&data->type_instance, child,
          /* may be empty = */ 1);
    else if (strcasecmp ("MetaData", child->key) == 0)
      status = ts_config_add_meta (&data->meta, child,
          /* may be empty = */ 1);
    else if (strcasecmp ("DeleteMetaData", child->key) == 0)
      status = ts_config_add_meta_delete (&data->meta_delete, child);
    else
    {
      ERROR ("Target `set': The `%s' configuration option is not understood "
          "and will be ignored.", child->key);
      status = 0;
    }

    if (status != 0)
      break;
  }

  /* Additional sanity-checking */
  while (status == 0)
  {
    if ((data->host == NULL)
        && (data->plugin == NULL)
        && (data->plugin_instance == NULL)
        /* && (data->type == NULL) */
        && (data->type_instance == NULL)
        && (data->meta == NULL)
        && (data->meta_delete == NULL))
    {
      ERROR ("Target `set': You need to set at least one of `Host', "
          "`Plugin', `PluginInstance', `TypeInstance', "
          "`MetaData', or `DeleteMetaData'.");
      status = -1;
    }

    if (data->meta != NULL)
    {
      /* If data->meta_delete is NULL, this loop is a no-op. */
      for (ts_key_list_t *l=data->meta_delete; l != NULL; l = l->next)
      {
        if (meta_data_type (data->meta, l->key) != 0)
        {
          /* MetaData and DeleteMetaData for the same key. */
          ERROR ("Target `set': Can only have one of `MetaData' or "
              "`DeleteMetaData' for any given key.");
          status = -1;
        }
      }
    }

    break;
  }

  if (status != 0)
  {
    ts_destroy ((void *) &data);
    return (status);
  }

  *user_data = data;
  return (0);
} /* }}} int ts_create */

static int ts_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  ts_data_t *data;

  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
  {
    ERROR ("Target `set': Invoke: `data' is NULL.");
    return (-EINVAL);
  }

  if (data->meta != NULL)
  {
    meta_data_clone_merge(&(vl->meta), data->meta);
  }

  /* If data->meta_delete is NULL, this loop is a no-op. */
  for (ts_key_list_t *l=data->meta_delete; l != NULL; l = l->next)
  {
    meta_data_delete(vl->meta, l->key);
  }

#define SET_FIELD(f) if (data->f != NULL) { sstrncpy (vl->f, data->f, sizeof (vl->f)); }
  SET_FIELD (host);
  SET_FIELD (plugin);
  SET_FIELD (plugin_instance);
  /* SET_FIELD (type); */
  SET_FIELD (type_instance);

  return (FC_TARGET_CONTINUE);
} /* }}} int ts_invoke */

void module_register (void)
{
	target_proc_t tproc = { 0 };

	tproc.create  = ts_create;
	tproc.destroy = ts_destroy;
	tproc.invoke  = ts_invoke;
	fc_register_target ("set", tproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */

