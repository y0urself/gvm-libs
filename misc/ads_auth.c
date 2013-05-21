/* OpenVAS Libraries
 * $Id$
 * Description: LDAP/ADS Authentication module.
 *
 * Authors:
 * Felix Wolfsteller <felix.wolfsteller@intevation.de>
 *
 * Copyright:
 * Copyright (C) 2010 Greenbone Networks GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * or, at your option, any later version as published by the Free
 * Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef ENABLE_LDAP_AUTH

#define _GNU_SOURCE
#include "ads_auth.h"
#include "ldap_auth.h"

#include <stdio.h>
#include <stdlib.h>             /* for free */
#include <string.h>             /* for strcasestr */

#include <glib.h>

#include <ldap.h>

#include "openvas_auth.h"
#include "openvas_string.h"

#define KEY_ADS_DOMAIN "domain"

#undef G_LOG_DOMAIN
/**
 * @brief GLib logging domain.
 */
#define G_LOG_DOMAIN "lib   ads"

/**
 * @file ads_auth.c
 *
 * Contains structs and functions to use for basic authentication against
 * an ADS (Active Directory Server). Most functionality is provided by the
 * ldap_auth module.
 *
 * @section adsprep ADS Preparations
 * Currently, the approach for ADS is not to use the schema as for the ldap
 * authenticator, but instead use group memberships.
 *
 * It is assumed that three groups exist under
 * OU=GSM Roles,OU=greenbone,DC=YOURDOMAIN .
 * These are CN=GSM User , CN=GSM Admin and CN=GSM None  and membership
 * specifies the role of a user.
 *
 * Rules are also groups. The ruletype is specified by the organizational unit
 * a "rule"-group is in. The OUs for the ruletypes are:
 * x,OU=GSM Accessrules,OU=greenbone
 * with x = OU=GSM Rule Allow , OU=GSM Rule Deny or OU=GSM Rule Allow All
 * Groups within these containers need to specify the targets ("rules") in
 * their info attribute.
 *
 * This setup allows relatively easy management of user roles and rules via
 * the standard ADS configuration and management tools.
 */

/**
 * @brief From domain.org returns dc=domain,dc=org to be used with ldap
 * @brief queries.
 *
 * @param[in] domain The domain in "dot-notation"
 *
 * @return The domain as ldap dc's to be used with ldap-queries.
 */
static gchar *
domain_to_ldap_dc (const gchar * domain)
{
  if (domain == NULL)
    return NULL;

  gchar **domain_components = g_strsplit (domain, ".", -1);
  gchar *part = g_strjoinv (",dc=", domain_components);
  // Now we have "domain,dc=org"
  g_strfreev (domain_components);
  gchar *result = g_strconcat ("dc=", part, NULL);
  g_free (part);

  return result;
}


/**
 * @brief Creates a ads_auth_info_t struct from Key-file.
 *
 * @param[in] key_file Key-file to read values in from.
 * @param[in] group    Group in key-file.
 *
 * @return Freshly allocated ads_auth_info in case all mandatory values where
 *         found. NULL otherwise.
 */
ads_auth_info_t
ads_auth_info_from_key_file (GKeyFile * key_file, const gchar * group)
{
  if (key_file == NULL || group == NULL)
    return NULL;

  ldap_auth_info_t ldapinfo = ldap_auth_info_from_key_file (key_file, group);
  gchar *ads_domain;

  if (ldapinfo == NULL)
    {
      g_debug ("LDAP Configuration of ADS not found.");
      return NULL;
    }

  ads_auth_info_t info = g_malloc0 (sizeof (struct ads_auth_info));
  info->ldap_auth_conf = ldapinfo;
  ads_domain = g_key_file_get_string (key_file, group, KEY_ADS_DOMAIN, NULL);

  if (ads_domain == NULL)
    {
      g_warning ("Domain is not specified in ADS/LDAP Configuration.");
      ads_auth_info_free (info);
      return NULL;
    }

  info->domain = ads_domain;
  info->domain_dc = domain_to_ldap_dc (info->domain);

  return info;
}


/**
 * @brief Frees an ads_auth_info struct.
 *
 * @param[in] info The struct to free, can be NULL.
 */
void
ads_auth_info_free (ads_auth_info_t info)
{
  if (!info)
    return;

  ldap_auth_info_free (info->ldap_auth_conf);

  g_free (info->domain);
  g_free (info->domain_dc);
  g_free (info);
}

/**
 * @brief Find value(s) of an attribute of an object.
 *
 * @param[in] ldap      The ldap handle to use.
 * @param[in] dn        DN of the object to search.
 * @param[in] attribute The attribute whose value to query.
 *
 * @return List of gchar*s, to be freed by caller. NULL for empty list or
 *         error.
 */
GSList *
ldap_object_get_attribute_values (LDAP * ldap, const gchar * dn,
                                  gchar * attribute)
{
  char *attrs[] = { attribute, NULL };
  char *attr_it = NULL;
  struct berval **attr_vals = NULL;
  BerElement *ber = NULL;
  LDAPMessage *result, *result_it;
  GSList *string_list = NULL;

  int res = ldap_search_ext_s (ldap, dn /* base */ , LDAP_SCOPE_BASE,
                               NULL /* filter */ , attrs, 0 /* attrsonly */ ,
                               NULL /* serverctrls */ , NULL /* clientctrls */ ,
                               LDAP_NO_LIMIT,   /* timeout */
                               LDAP_NO_LIMIT,   /* sizelimit */
                               &result);
  if (res != LDAP_SUCCESS)
    {
      g_debug ("LDAP Query in %s failed: %s", __FUNCTION__,
               ldap_err2string (res));
      return FALSE;
    }

  result_it = ldap_first_entry (ldap, result);
  if (result_it != NULL)
    {
      // Get the first (and only) attribute in the entry.
      attr_it = ldap_first_attribute (ldap, result_it, &ber);
      if (attr_it != NULL)
        {
          /* Get the attribute values. */
          attr_vals = ldap_get_values_len (ldap, result_it, attr_it);
          if (attr_vals != NULL)
            {
              struct berval **attr_vals_it = attr_vals;
              while (attr_vals_it && *attr_vals_it)
                {
                  string_list =
                    g_slist_prepend (string_list,
                                     g_strdup ((*attr_vals_it)->bv_val));
                  attr_vals_it++;
                }

              ldap_value_free_len (attr_vals);
            }
          else
            {
              g_debug ("Empty result of LDAP query for attribute values.");
            }
          ldap_memfree (attr_it);
        }
      else
        {
          g_debug ("LDAP query searched for non-existing attribute.");
        }
      if (ber != NULL)
        {
          ber_free (ber, 0);
        }
    }

  ldap_msgfree (result);

  return string_list;
}


/** @todo refactor/merge with ldap_auth module. */

/**
 * @brief Binds to an ADS and returns result of a query.
 *
 * @param[in] host       The host to connect to.
 * @param[in] domain     The domain to connect to.
 * @param[in] dn         The dn whose subtree to query.
 * @param[in] username   Username to authenticate with.
 * @param[in] password   Password for user@domain.
 * @param[in] filter     The filter for query (e.g. "(objectClass=person)").
 * @param[in] attribute  The attribute to query (e.g. "gender").
 *
 * @return List of strings (values of attribute of objects matching filter).
 */
GSList *
ads_auth_bind_query (const gchar * host, const char *domain, const char *dn,
                     const gchar * username, const gchar * password,
                     const gchar * filter, const gchar * attribute)
{
  GSList *attribute_values = NULL;
  gchar *authdn = g_strconcat (username, "@", domain, NULL);
  LDAP *ldap = ldap_auth_bind (host, authdn, password, FALSE);
  g_free (authdn);

  if (!ldap)
    {
      g_warning ("LDAP Connection for query failed.");
    }

  attribute_values = ldap_auth_query (ldap, dn, filter, attribute);

  if (ldap)
    ldap_unbind_ext_s (ldap, NULL, NULL);

  return attribute_values;
}


/**
 * @brief Finds out whether an objects attribute has a certain value.
 *
 * Works for multi-valued attributes.
 *
 * @param[in] ldap      The ldap handle to use.
 * @param[in] dn        DN of the object to search.
 * @param[in] attribute The attribute whose value to query.
 * @param[in] value     The value to match attribute values against.
 *
 * @return TRUE if object with \ref dn has the \ref attribute with the given
 *         \ref value . FALSE otherwise.
 */
gboolean
ldap_object_attribute_has_value (LDAP * ldap, const gchar * dn,
                                 gchar * attribute, const gchar * value)
{
  GSList *attr_vals = ldap_object_get_attribute_values (ldap, dn, attribute);
  GSList *attr_vals_it = attr_vals;
  gboolean found = FALSE;

  while (attr_vals_it)
    {
      if (strcmp (attr_vals_it->data, value) == 0)
        {
          found = TRUE;
          break;
        }

      attr_vals_it = g_slist_next (attr_vals_it);
    }

  openvas_string_list_free (attr_vals);
  return found;
}


/**
 * @brief Queries the DN of an users object.
 *
 * @param[in] ldap     LDAP-Handle to use.
 * @param[in] username Account name to search for.
 * @param[in] domain   Domain for ldap query (e.g. "dc=company,dc=org").
 *
 * @return DN of user object, NULL in case of errors / not found. Caller has
 *         to free.
 */
static char *
ads_query_user_dn (LDAP * ldap, const gchar * username, const gchar * domain)
{
  LDAPMessage *result;
  char *dn = NULL;
  int res = ldap_search_ext_s (ldap, domain /* base */ ,
                               LDAP_SCOPE_SUBTREE,      /* scope */
                               ("(&(objectClass=user)(sAMAccountName=gsmtest))")
                               /* filter */ , NULL /*attrs */ ,
                               0 /* attrsonly */ ,
                               NULL /* serverctrls */ , NULL /* clientctrls */ ,
                               LDAP_NO_LIMIT,   /* timeout */
                               LDAP_NO_LIMIT,   /* sizelimit */
                               &result);

  // The query failed, handle as error.
  if (res != LDAP_SUCCESS)
    {
      g_debug ("The dn of an ads user could not be found: %s",
               ldap_err2string (res));
      return NULL;
    }

  // No or not a distinct result, handle as error.
  if (ldap_count_entries (ldap, result) != 1)
    {
      g_debug ("The dn of a user could not be found: %d results were "
               "returned.", ldap_count_entries (ldap, result));
      return NULL;
    }

  dn = ldap_get_dn (ldap, result);
  ldap_msgfree (result);

  return dn;
}


/**
 * @brief Authenticate against an ldap directory server.
 *
 * @param info      Schema and adress to use.
 * @param username  Username to authenticate.
 * @param password  Password to use.
 *
 * @return 0 authentication success, 1 authentication failure, -1 error.
 */
int
ads_authenticate (const gchar * username, const gchar * password,
                  /*const *//*ads_auth_info_t */ void *ads_auth_info)
{
  ldap_auth_info_t info = ((ads_auth_info_t) ads_auth_info)->ldap_auth_conf;
  ads_auth_info_t ads_info = (ads_auth_info_t) ads_auth_info;

  if (info == NULL || username == NULL || password == NULL || !info->ldap_host
      || ads_info->domain)
    return -1;

  LDAP *ldap;
  gchar *authdn = NULL;
  char *dn = NULL;

  authdn = g_strconcat (username, "@", ads_info->domain, NULL);
  ldap =
    ldap_auth_bind (info->ldap_host, authdn, password,
                    (info->allow_plaintext == FALSE) ? TRUE : FALSE);
  g_free (authdn);

  if (ldap == NULL)
    return -1;

  // Get the "real" DN by searching for samAccountName=user .
  dn = ads_query_user_dn (ldap, username, ads_info->domain_dc);

  ldap_unbind_ext_s (ldap, NULL, NULL);
  g_free (authdn);
  free (dn);

  return 1;
}

#endif /* ENABLE_LDAP_AUTH */
