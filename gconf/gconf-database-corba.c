/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
 * Copyright (C) 2003       CodeFactory AB
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gconf-database-corba.h"
#include "gconf-corba-utils.h"
#include "gconfd.h"
#include "gconfd-corba.h"
#include "gconf-locale.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define DB_FROM_SERVANT(servant) (((CorbaData *)servant)->db)

/*
 * Forward decls
 */

typedef struct _Listener Listener;

struct _Listener {
  GConfDatabaseListener parent;

  ConfigListener obj;
};

typedef struct
{
  POA_ConfigDatabase3 servant;

  ConfigDatabase objref;

  GConfDatabase *db;
} CorbaData;

static Listener* listener_new (ConfigListener obj,
                               const char    *name);
static void      listener_destroy (Listener* l);


/*
 * CORBA implementation of ConfigDatabase
 */

static CORBA_unsigned_long
impl_ConfigDatabase_add_listener(PortableServer_Servant servant,
				 const CORBA_char * where,
				 const ConfigListener who,
                                 CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);

  if (gconfd_corba_check_in_shutdown (ev))
    return 0;
  
  return gconf_database_corba_add_listener (db, who, NULL, where);
}

static void
impl_ConfigDatabase_remove_listener(PortableServer_Servant servant,
				    CORBA_unsigned_long cnxn,
				    CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  gconf_database_corba_remove_listener (db, cnxn);
}

static ConfigValue*
impl_ConfigDatabase_lookup_with_locale(PortableServer_Servant servant,
                                       const CORBA_char * key,
                                       const CORBA_char * locale,
                                       CORBA_boolean use_schema_default,
                                       CORBA_boolean * value_is_default,
                                       CORBA_boolean * value_is_writable,
                                       CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GConfValue* val;
  GError* error = NULL;
  GConfLocaleList* locale_list;
  gboolean is_default = FALSE;
  gboolean is_writable = TRUE;

  if (gconfd_corba_check_in_shutdown (ev))
    return gconf_invalid_corba_value ();
  
  locale_list = gconfd_locale_cache_lookup(locale);
  
  val = gconf_database_query_value(db, key, locale_list->list,
                                   use_schema_default,
                                   NULL,
                                   &is_default,
                                   &is_writable,
                                   &error);

  *value_is_default = is_default;
  *value_is_writable = is_writable;
  
  gconf_locale_list_unref(locale_list);
  
  if (val != NULL)
    {
      ConfigValue* cval = gconf_corba_value_from_gconf_value(val);

      gconf_value_free(val);

      g_return_val_if_fail(error == NULL, cval);
      
      return cval;
    }
  else
    {
      gconf_corba_set_exception(&error, ev);

      return gconf_invalid_corba_value ();
    }
}

static ConfigValue *
impl_ConfigDatabase_lookup(PortableServer_Servant servant,
                           const CORBA_char * key,
                           CORBA_Environment * ev)
{
  if (gconfd_corba_check_in_shutdown (ev))
    return gconf_invalid_corba_value ();
  
  return impl_ConfigDatabase_lookup_with_locale (servant, key,
                                                 NULL, TRUE, NULL,
                                                 NULL, ev);
}

static ConfigValue*
impl_ConfigDatabase_lookup_default_value(PortableServer_Servant servant,
                                         const CORBA_char * key,
                                         const CORBA_char * locale,
                                         CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GConfValue* val;
  GError* error = NULL;
  GConfLocaleList* locale_list;  

  if (gconfd_corba_check_in_shutdown (ev))
    return gconf_invalid_corba_value ();
  
  locale_list = gconfd_locale_cache_lookup(locale);
  
  val = gconf_database_query_default_value(db, key,
                                           locale_list->list,
                                           NULL,
                                           &error);

  gconf_locale_list_unref(locale_list);
  
  if (val != NULL)
    {
      ConfigValue* cval = gconf_corba_value_from_gconf_value(val);

      gconf_value_free(val);

      g_return_val_if_fail(error == NULL, cval);
      
      return cval;
    }
  else
    {
      gconf_corba_set_exception(&error, ev);

      return gconf_invalid_corba_value ();
    }
}

static void
impl_ConfigDatabase_batch_lookup(PortableServer_Servant servant,
				 const ConfigDatabase_KeyList * keys,
				 const CORBA_char * locale,
				 ConfigDatabase_ValueList ** values,
				 ConfigDatabase_IsDefaultList ** is_defaults,
				 ConfigDatabase_IsWritableList ** is_writables,
                                 CORBA_Environment * ev)
{
  if (gconfd_corba_check_in_shutdown (ev))
    return;
}

static void
impl_ConfigDatabase_set(PortableServer_Servant servant,
			const CORBA_char * key,
			const ConfigValue * value,
                        CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GConfValue* val;
  GError* error = NULL;

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  if (value->_d == InvalidVal)
    {
      gconf_log(GCL_ERR, _("Received invalid value in set request"));
      return;
    }

  val = gconf_value_from_corba_value(value);

  if (val == NULL)
    {
      gconf_log(GCL_ERR, _("Couldn't make sense of CORBA value received in set request for key `%s'"), key);
      return;
    }

#if 0
  {
    gchar* str = gconf_value_to_string(val);
 
    /* reduce traffice to the logfile */
    gconf_log(GCL_DEBUG, "Received request to set key `%s' to `%s'", key, str);

    g_free(str);
  }
#endif

  gconf_database_set(db, key, val, &error);

  gconf_corba_set_exception(&error, ev);

  gconf_value_free(val);
}

static void
impl_ConfigDatabase_unset_with_locale (PortableServer_Servant servant,
                                       const CORBA_char * key,
                                       const CORBA_char * locale,
                                       CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GError* error = NULL;

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  gconf_database_unset(db, key, locale, &error);

  gconf_corba_set_exception(&error, ev);
}

static void
impl_ConfigDatabase_unset(PortableServer_Servant servant,
			  const CORBA_char * key,
                          CORBA_Environment * ev)
{
  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  /* This is a cheat, since const CORBA_char* isn't normally NULL */
  impl_ConfigDatabase_unset_with_locale (servant, key, NULL, ev);
}

static void
impl_ConfigDatabase_batch_change (PortableServer_Servant servant,
                                  const CORBA_char * locale,
                                  const ConfigDatabase_KeyList * keys,
                                  const ConfigDatabase_ValueList * values,
                                  CORBA_Environment * ev)
{
  if (gconfd_corba_check_in_shutdown (ev))
    return;
}

static CORBA_boolean
impl_ConfigDatabase_dir_exists(PortableServer_Servant servant,
			       const CORBA_char * dir,
                               CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  CORBA_boolean retval;
  GError* error = NULL;  

  if (gconfd_corba_check_in_shutdown (ev))
    return CORBA_FALSE;
  
  retval =
    gconf_database_dir_exists (db, dir, &error) ? CORBA_TRUE : CORBA_FALSE;

  gconf_corba_set_exception(&error, ev);

  return retval;
}

static void
impl_ConfigDatabase_remove_dir(PortableServer_Servant servant,
			       const CORBA_char * dir,
                               CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GError* error = NULL;  

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  gconf_database_remove_dir(db, dir, &error);

  gconf_corba_set_exception(&error, ev);
}

static void
impl_ConfigDatabase_all_entries(PortableServer_Servant servant,
				const CORBA_char * dir,
				const CORBA_char * locale,
				ConfigDatabase_KeyList ** keys,
				ConfigDatabase_ValueList ** values,
				ConfigDatabase_IsDefaultList ** is_defaults,
                                ConfigDatabase_IsWritableList ** is_writables,
				CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GSList* pairs;
  guint n;
  GSList* tmp;
  guint i;
  GError* error = NULL;
  GConfLocaleList* locale_list;  

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  locale_list = gconfd_locale_cache_lookup(locale);
  
  pairs = gconf_database_all_entries(db, dir, locale_list->list, &error);
  
  gconf_locale_list_unref(locale_list);

  if (error != NULL)
    {
      gconf_corba_set_exception(&error, ev);
      return;
    }
  
  n = g_slist_length(pairs);

  *keys= ConfigDatabase_KeyList__alloc();
  (*keys)->_buffer = CORBA_sequence_CORBA_string_allocbuf(n);
  (*keys)->_length = n;
  (*keys)->_maximum = n;
  (*keys)->_release = CORBA_TRUE; /* free buffer */
  
  *values= ConfigDatabase_ValueList__alloc();
  (*values)->_buffer = CORBA_sequence_ConfigValue_allocbuf(n);
  (*values)->_length = n;
  (*values)->_maximum = n;
  (*values)->_release = CORBA_TRUE; /* free buffer */

  *is_defaults = ConfigDatabase_IsDefaultList__alloc();
  (*is_defaults)->_buffer = CORBA_sequence_CORBA_boolean_allocbuf(n);
  (*is_defaults)->_length = n;
  (*is_defaults)->_maximum = n;
  (*is_defaults)->_release = CORBA_TRUE; /* free buffer */

  *is_writables = ConfigDatabase_IsWritableList__alloc();
  (*is_writables)->_buffer = CORBA_sequence_CORBA_boolean_allocbuf(n);
  (*is_writables)->_length = n;
  (*is_writables)->_maximum = n;
  (*is_writables)->_release = CORBA_TRUE; /* free buffer */
  
  tmp = pairs;
  i = 0;

  while (tmp != NULL)
    {
      GConfEntry* p = tmp->data;

      g_assert(p != NULL);
      g_assert(p->key != NULL);

      (*keys)->_buffer[i] = CORBA_string_dup(p->key);
      gconf_fill_corba_value_from_gconf_value (gconf_entry_get_value (p),
                                               &((*values)->_buffer[i]));
      (*is_defaults)->_buffer[i] = gconf_entry_get_is_default(p);
      (*is_writables)->_buffer[i] = gconf_entry_get_is_writable(p);
      
      gconf_entry_free(p);

      ++i;
      tmp = g_slist_next(tmp);
    }
  
  g_assert(i == n);

  g_slist_free(pairs);
}

static void
impl_ConfigDatabase_all_dirs(PortableServer_Servant servant,
			     const CORBA_char * dir,
			     ConfigDatabase_KeyList ** keys,
			     CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GSList* subdirs;
  guint n;
  GSList* tmp;
  guint i;
  GError* error = NULL;

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  subdirs = gconf_database_all_dirs (db, dir, &error);

  if (error != NULL)
    {
      /* I think this is right anyway... */
      gconf_corba_set_exception (&error, ev);
      *keys = NULL;
      return;
    }
  
  n = g_slist_length (subdirs);

  *keys = ConfigDatabase_KeyList__alloc();
  (*keys)->_buffer = CORBA_sequence_CORBA_string_allocbuf(n);
  (*keys)->_length = n;
  (*keys)->_maximum = n;
  (*keys)->_release = CORBA_TRUE; /* free buffer */
  
  tmp = subdirs;
  i = 0;

  while (tmp != NULL)
    {
      gchar* subdir = tmp->data;

      (*keys)->_buffer[i] = CORBA_string_dup (subdir);

      g_free (subdir);

      ++i;
      tmp = g_slist_next (tmp);
    }
  
  g_assert (i == n);
  
  g_slist_free (subdirs);
}

static void
impl_ConfigDatabase_set_schema (PortableServer_Servant servant,
                                const CORBA_char * key,
                                const CORBA_char * schema_key,
                                CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GError* error = NULL;

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  gconf_database_set_schema (db, key,
			     schema_key,
                             &error);
  
  gconf_corba_set_exception (&error, ev);
}

static void
impl_ConfigDatabase_sync (PortableServer_Servant servant,
                          CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GError* error = NULL;

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  gconf_database_sync (db, &error);

  gconf_corba_set_exception (&error, ev);
}

static void
impl_ConfigDatabase_clear_cache(PortableServer_Servant servant,
				CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GError* error = NULL;

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  gconf_log (GCL_DEBUG, _("Received request to drop all cached data"));  
  
  gconf_database_clear_cache (db, &error);

  gconf_corba_set_exception (&error, ev);
}

static void
impl_ConfigDatabase_synchronous_sync (PortableServer_Servant servant,
                                      CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GError* error = NULL;

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  gconf_log(GCL_DEBUG, _("Received request to sync synchronously"));
  
  
  gconf_database_synchronous_sync (db, &error);

  gconf_corba_set_exception (&error, ev);
}


static ConfigValue*
impl_ConfigDatabase2_lookup_with_schema_name(PortableServer_Servant servant,
                                             const CORBA_char * key,
                                             const CORBA_char * locale,
                                             CORBA_boolean use_schema_default,
                                             CORBA_char    **schema_name,
                                             CORBA_boolean * value_is_default,
                                             CORBA_boolean * value_is_writable,
                                             CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GConfValue* val;
  GError* error = NULL;
  GConfLocaleList* locale_list;
  gboolean is_default = FALSE;
  gboolean is_writable = TRUE;
  char *s;
  ConfigValue* cval;
  
  if (gconfd_corba_check_in_shutdown (ev))
    return gconf_invalid_corba_value ();
  
  locale_list = gconfd_locale_cache_lookup(locale);

  s = NULL;
  val = gconf_database_query_value(db, key, locale_list->list,
                                   use_schema_default,
                                   &s,
                                   &is_default,
                                   &is_writable,
                                   &error);

  *value_is_default = is_default;
  *value_is_writable = is_writable;

  if (s)
    *schema_name = CORBA_string_dup (s);
  else
    *schema_name = CORBA_string_dup ("");

  g_free (s);
  
  gconf_locale_list_unref(locale_list);
  
  if (val != NULL)
    {
      cval = gconf_corba_value_from_gconf_value(val);
      gconf_value_free(val);
      g_return_val_if_fail(error == NULL, cval);
    }
  else
    {
      cval = gconf_invalid_corba_value ();
    }

  gconf_log (GCL_DEBUG, "In lookup_with_schema_name returning schema name '%s' error '%s'",
             *schema_name, error ? error->message : "none");
  
  if (error != NULL)
    {
      gconf_corba_set_exception (&error, ev);
    }

  return cval;
}

static void
impl_ConfigDatabase2_all_entries_with_schema_name(PortableServer_Servant servant,
                                                  const CORBA_char * dir,
                                                  const CORBA_char * locale,
                                                  ConfigDatabase_KeyList ** keys,
                                                  ConfigDatabase_ValueList ** values,
                                                  ConfigDatabase2_SchemaNameList **schema_names,
                                                  ConfigDatabase_IsDefaultList   **is_defaults,
                                                  ConfigDatabase_IsWritableList  **is_writables,
                                                  CORBA_Environment * ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GSList* pairs;
  guint n;
  GSList* tmp;
  guint i;
  GError* error = NULL;
  GConfLocaleList* locale_list;  

  if (gconfd_corba_check_in_shutdown (ev))
    return;
  
  locale_list = gconfd_locale_cache_lookup(locale);
  
  pairs = gconf_database_all_entries(db, dir, locale_list->list, &error);
  
  gconf_locale_list_unref(locale_list);

  if (error != NULL)
    {
      gconf_corba_set_exception(&error, ev);
      return;
    }
  
  n = g_slist_length(pairs);

  *keys= ConfigDatabase_KeyList__alloc();
  (*keys)->_buffer = CORBA_sequence_CORBA_string_allocbuf(n);
  (*keys)->_length = n;
  (*keys)->_maximum = n;
  (*keys)->_release = CORBA_TRUE; /* free buffer */
  
  *values= ConfigDatabase_ValueList__alloc();
  (*values)->_buffer = CORBA_sequence_ConfigValue_allocbuf(n);
  (*values)->_length = n;
  (*values)->_maximum = n;
  (*values)->_release = CORBA_TRUE; /* free buffer */

  *schema_names = ConfigDatabase2_SchemaNameList__alloc();
  (*schema_names)->_buffer = CORBA_sequence_CORBA_string_allocbuf(n);
  (*schema_names)->_length = n;
  (*schema_names)->_maximum = n;
  (*schema_names)->_release = CORBA_TRUE; /* free buffer */
  
  *is_defaults = ConfigDatabase_IsDefaultList__alloc();
  (*is_defaults)->_buffer = CORBA_sequence_CORBA_boolean_allocbuf(n);
  (*is_defaults)->_length = n;
  (*is_defaults)->_maximum = n;
  (*is_defaults)->_release = CORBA_TRUE; /* free buffer */

  *is_writables = ConfigDatabase_IsWritableList__alloc();
  (*is_writables)->_buffer = CORBA_sequence_CORBA_boolean_allocbuf(n);
  (*is_writables)->_length = n;
  (*is_writables)->_maximum = n;
  (*is_writables)->_release = CORBA_TRUE; /* free buffer */
  
  tmp = pairs;
  i = 0;

  while (tmp != NULL)
    {
      GConfEntry* p = tmp->data;

      g_assert(p != NULL);
      g_assert(p->key != NULL);

      (*keys)->_buffer[i] = CORBA_string_dup (p->key);
      gconf_fill_corba_value_from_gconf_value (gconf_entry_get_value (p),
                                               &((*values)->_buffer[i]));
      (*schema_names)->_buffer[i] = CORBA_string_dup (gconf_entry_get_schema_name (p));
      if ((*schema_names)->_buffer[i] == NULL)
        (*schema_names)->_buffer[i] = CORBA_string_dup ("");
      (*is_defaults)->_buffer[i] = gconf_entry_get_is_default(p);
      (*is_writables)->_buffer[i] = gconf_entry_get_is_writable(p);
      
      gconf_entry_free(p);

      ++i;
      tmp = g_slist_next(tmp);
    }
  
  g_assert(i == n);

  g_slist_free(pairs);
}

static CORBA_unsigned_long
impl_ConfigDatabase3_add_listener_with_properties (PortableServer_Servant servant,
                                                   const CORBA_char *where,
                                                   const ConfigListener who,
                                                   const ConfigDatabase3_PropList *properties,
                                                   CORBA_Environment *ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  const char *name = NULL;
  int i;
  
  if (gconfd_corba_check_in_shutdown (ev))
    return 0;

  i = 0;
  while (i < (int) properties->_length)
    {
      if (strcmp (properties->_buffer[i].key, "name") == 0)
        name = properties->_buffer[i].value;

      ++i;
    }
  
  return gconf_database_corba_add_listener (db, who, name, where);
}

static void
impl_ConfigDatabase3_recursive_unset (PortableServer_Servant servant,
                                      const CORBA_char *key,
                                      ConfigDatabase3_UnsetFlags flags,
                                      CORBA_Environment *ev)
{
  GConfDatabase *db = DB_FROM_SERVANT (servant);
  GError *error;
  GConfUnsetFlags gconf_flags;
  
  if (gconfd_corba_check_in_shutdown (ev))
    return;

  gconf_flags = 0;
  if (flags & ConfigDatabase3_UNSET_INCLUDING_SCHEMA_NAMES)
    gconf_flags |= GCONF_UNSET_INCLUDING_SCHEMA_NAMES;
  
  error = NULL;
  gconf_database_recursive_unset (db, key, NULL, gconf_flags, &error);

  gconf_corba_set_exception (&error, ev);
}

static PortableServer_ServantBase__epv base_epv = {
  NULL,
  NULL,
  NULL
};

static POA_ConfigDatabase__epv server_epv = { 
  NULL,
  impl_ConfigDatabase_add_listener,
  impl_ConfigDatabase_remove_listener,
  impl_ConfigDatabase_lookup,
  impl_ConfigDatabase_lookup_with_locale,
  impl_ConfigDatabase_lookup_default_value,
  impl_ConfigDatabase_batch_lookup,
  impl_ConfigDatabase_set,
  impl_ConfigDatabase_unset,
  impl_ConfigDatabase_unset_with_locale,
  impl_ConfigDatabase_batch_change,
  impl_ConfigDatabase_dir_exists,
  impl_ConfigDatabase_remove_dir,
  impl_ConfigDatabase_all_entries,
  impl_ConfigDatabase_all_dirs,
  impl_ConfigDatabase_set_schema,
  impl_ConfigDatabase_sync,
  impl_ConfigDatabase_clear_cache,
  impl_ConfigDatabase_synchronous_sync
};

static POA_ConfigDatabase2__epv server2_epv = { 
  NULL,
  impl_ConfigDatabase2_lookup_with_schema_name,
  impl_ConfigDatabase2_all_entries_with_schema_name
};

static POA_ConfigDatabase3__epv server3_epv = { 
  NULL,
  impl_ConfigDatabase3_add_listener_with_properties,
  impl_ConfigDatabase3_recursive_unset
};

static POA_ConfigDatabase3__vepv poa_server_vepv = { &base_epv, &server_epv, &server2_epv, &server3_epv };


void
gconf_database_corba_deinit (GConfDatabase *db)
{
  CorbaData *data = db->corba_data;
  
  PortableServer_ObjectId *oid;
  CORBA_Environment ev;

  CORBA_exception_init (&ev);
  
  CORBA_Object_release (data->objref, &ev);

  CORBA_exception_free (&ev);
  
  oid = PortableServer_POA_servant_to_id (gconf_corba_get_poa(), &data->servant, &ev);

  CORBA_exception_free (&ev);
  
  PortableServer_POA_deactivate_object (gconf_corba_get_poa (), oid, &ev);

  CORBA_exception_free (&ev);
  
  POA_ConfigDatabase3__fini (&data->servant, &ev);

  CORBA_free (oid);

  CORBA_exception_free (&ev);

  g_free (data);
  db->corba_data = NULL;
}

void
gconf_database_corba_init (GConfDatabase *db)
{
  CORBA_Environment ev;
  CorbaData *data = g_new0 (CorbaData, 1);

  data->servant._private = NULL;
  data->servant.vepv = &poa_server_vepv;
  data->db = db;
  
  CORBA_exception_init (&ev);
  
  POA_ConfigDatabase3__init (&data->servant, &ev);

  data->objref = PortableServer_POA_servant_to_reference (gconf_corba_get_poa (),
							  &data->servant,
							  &ev);
  if (CORBA_Object_is_nil(data->objref, &ev))
    {
      gconf_log(GCL_ERR,
                _("Fatal error: failed to get object reference for ConfigDatabase"));
      
      exit (1);
    }

  db->corba_data = data;
}

ConfigDatabase
gconf_database_corba_get_objref (GConfDatabase *db)
{
  CorbaData *data;

  data = db->corba_data;

  return data->objref;
}


CORBA_unsigned_long
gconf_database_corba_readd_listener   (GConfDatabase       *db,
				       ConfigListener       who,
				       const char          *name,
				       const gchar         *where)
{
  Listener* l;
  guint cnxn;
  
  gconfd_need_log_cleanup ();
  
  g_assert(db->listeners != NULL);

  db->last_access = time(NULL);
  
  l = listener_new (who, name);

  cnxn = gconf_listeners_add (db->listeners, where, l,
                              (GFreeFunc)listener_destroy);

  if (l->parent.name == NULL)
    l->parent.name = g_strdup_printf ("%u", cnxn);
  
  gconf_log (GCL_DEBUG, "Added listener %s (%u)", l->parent.name, cnxn);
  
  return cnxn;
}

CORBA_unsigned_long
gconf_database_corba_add_listener    (GConfDatabase       *db,
				      ConfigListener       who,
				      const char          *name,
				      const gchar         *where)
{
  GError *err;
  CORBA_unsigned_long cnxn;  
  
  gconfd_need_log_cleanup ();
  
  cnxn = gconf_database_corba_readd_listener (db, who, name, where);
  
  err = NULL;
  if (!gconfd_logfile_change_listener (db, TRUE, cnxn,
                                       who, where, &err))
    {
      /* This error is not fatal; we basically ignore it.
       * Because it's likely the right thing for the client
       * app to simply continue.
       */
      gconf_log (GCL_WARNING,
		 _("Failed to log addition of listener %s (%s);"
		   "will not be able to restore this listener on "
		   "gconfd restart, resulting in unreliable "
		   "notification of configuration changes."),
		 name, err->message);
      g_error_free (err);
    }
  
  return cnxn;
}

void
gconf_database_corba_remove_listener (GConfDatabase       *db,
				      CORBA_unsigned_long  cnxn)
{
  union {
    Listener *l;
    gpointer l_ptr;
  } l = { NULL };
  GError *err;
  const gchar *location = NULL;

  gconfd_need_log_cleanup ();
  
  g_assert(db->listeners != NULL);
  
  db->last_access = time(NULL);

  gconf_log(GCL_DEBUG, "Removing listener %u", (guint)cnxn);

  if (!gconf_listeners_get_data (db->listeners, cnxn,
				 &(l.l_ptr),
				 &location))
    {
      gconf_log (GCL_WARNING, _("Listener ID %lu doesn't exist"),
                 (gulong) cnxn);
      return;
    }
  else
    {
      gconf_log (GCL_DEBUG, "Name of listener %u is %s",
                 (guint) cnxn, l.l->parent.name);
    }
  
  err = NULL;
  if (!gconfd_logfile_change_listener (db, FALSE, cnxn,
                                       l.l->obj, location, &err))
    {
      gconf_log (GCL_WARNING, _("Failed to log removal of listener to logfile (most likely harmless, may result in a notification weirdly reappearing): %s"),
                 err->message);
      g_error_free (err);
    }
  
  /* calls destroy notify */
  gconf_listeners_remove (db->listeners, cnxn);
}


typedef struct _ListenerNotifyClosure ListenerNotifyClosure;

struct _ListenerNotifyClosure {
  GConfDatabase* db;
  ConfigValue* value;
  gboolean is_default;
  gboolean is_writable;
  GSList* dead;
  CORBA_Environment ev;
};

static void
notify_listeners_cb(GConfListeners* listeners,
                    const gchar* all_above_key,
                    guint cnxn_id,
                    gpointer listener_data,
                    gpointer user_data)
{
  Listener* l = listener_data;
  ListenerNotifyClosure* closure = user_data;

  if (l->parent.type != GCONF_DATABASE_LISTENER_CORBA)
    return;
  
  ConfigListener_notify(l->obj,
                        ((CorbaData *)closure->db->corba_data)->objref,
                        cnxn_id, 
                        (gchar*)all_above_key,
                        closure->value,
                        closure->is_default,
                        closure->is_writable,
                        &closure->ev);
  
  if(closure->ev._major != CORBA_NO_EXCEPTION) 
    {
      gconf_log (GCL_DEBUG, "Failed to notify listener %s (%u), removing: %s", 
                 l->parent.name, cnxn_id, CORBA_exception_id (&closure->ev));
      CORBA_exception_free (&closure->ev);
      
      /* Dead listeners need to be forgotten */
      closure->dead = g_slist_prepend(closure->dead, GUINT_TO_POINTER(cnxn_id));
    }
  else
    {
      gconf_log (GCL_DEBUG, "Notified listener %s (%u) of change to key `%s'",
                 l->parent.name, cnxn_id, all_above_key);
    }
}

void
gconf_database_corba_notify_listeners (GConfDatabase       *db,
				       const gchar         *key,
				       const GConfValue    *value,
				       gboolean             is_default,
				       gboolean             is_writable)
{
  ListenerNotifyClosure closure;
  GSList* tmp;
  
  g_return_if_fail(db != NULL);

  closure.db = db;
  closure.value = gconf_corba_value_from_gconf_value (value);
  closure.is_default = is_default;
  closure.is_writable = is_writable;
  closure.dead = NULL;
  
  CORBA_exception_init(&closure.ev);
  
  gconf_listeners_notify(db->listeners, key, notify_listeners_cb, &closure);

  tmp = closure.dead;

  if (tmp)
    gconfd_need_log_cleanup ();
  
  while (tmp != NULL)
    {
      guint dead = GPOINTER_TO_UINT(tmp->data);
      
      gconf_listeners_remove(db->listeners, dead);

      tmp = g_slist_next(tmp);
    }

  CORBA_free (closure.value);
}

static gboolean
client_alive_predicate (const gchar* location,
                        guint        cnxn_id,
                        gpointer     listener_data,
                        gpointer     user_data)
{
  Listener *l = listener_data;
  CORBA_Environment ev;
  CORBA_boolean result;

  if (l->parent.type != GCONF_DATABASE_LISTENER_CORBA)
    return FALSE;
  
  CORBA_exception_init (&ev);
  
  result = CORBA_Object_non_existent (l->obj, &ev);

  if (ev._major != CORBA_NO_EXCEPTION)
    {
      gconf_log (GCL_WARNING, "Exception from CORBA_Object_non_existent(), assuming stale listener %u",
                 cnxn_id);
      
      CORBA_exception_free (&ev);

      result = TRUE;
    }

  if (result)
    gconf_log (GCL_DEBUG, "Dropping dead listener %s (%u), appears to be nonexistent", l->parent.name, cnxn_id);
  
  return result;
}

void
gconf_database_corba_drop_dead_listeners (GConfDatabase *db)
{
  if (db->listeners)
    {
      gconf_listeners_remove_if (db->listeners,
                                 client_alive_predicate,
                                 NULL);
    }
}


struct ForeachData
{
  GString *str;
  gchar *db_name;
};

static void
listener_save_foreach (const gchar* location,
                       guint cnxn_id,
                       gpointer listener_data,
                       gpointer user_data)
{
  struct ForeachData *fd = user_data;
  Listener* l = listener_data;
  CORBA_ORB orb;
  CORBA_Environment ev;
  gchar *ior;
  gchar *s;

  if (l->parent.type != GCONF_DATABASE_LISTENER_CORBA)
    return;
  
  gconf_log (GCL_DEBUG, "Saving listener %s (%u) to log file", l->parent.name,
             (guint) cnxn_id);
  
  s = g_strdup_printf ("ADD %u %s ", cnxn_id, fd->db_name);

  g_string_append (fd->str, s);

  g_free (s);

  s = gconf_quote_string (location);
  g_string_append (fd->str, s);
  g_free (s);
  g_string_append_c (fd->str, ' ');
  
  orb = gconf_orb_get ();

  CORBA_exception_init (&ev);
  
  ior = CORBA_ORB_object_to_string(orb, l->obj, &ev);

  s = gconf_quote_string (ior);

  g_string_append (fd->str, s);

  g_free (s);
  
  CORBA_free(ior);

  g_string_append_c (fd->str, '\n');
}

void
gconf_database_log_listeners_to_string (GConfDatabase *db,
                                        gboolean is_default,
                                        GString *str)
{
  struct ForeachData fd;

  fd.str = str;
  
  if (is_default)
    fd.db_name = gconf_quote_string ("def");
  else
    {
      fd.db_name =
        gconf_quote_string (gconf_database_get_persistent_name (db));
    }
        
  gconf_listeners_foreach (db->listeners,
                           listener_save_foreach,
                           &fd);

  g_free (fd.db_name);
}

/*
 * The listener object
 */

static Listener* 
listener_new (ConfigListener obj,
              const char    *name)
{
  Listener* l;
  CORBA_Environment ev;

  CORBA_exception_init (&ev);

  l = g_new0 (Listener, 1);

  l->obj = CORBA_Object_duplicate (obj, &ev);
  l->parent.type = GCONF_DATABASE_LISTENER_CORBA;
  l->parent.name = g_strdup (name);
  
  return l;
}

static void      
listener_destroy (Listener* l)
{  
  CORBA_Environment ev;

  CORBA_exception_init (&ev);
  CORBA_Object_release (l->obj, &ev);
  g_free (l->parent.name);
  g_free (l);
}



