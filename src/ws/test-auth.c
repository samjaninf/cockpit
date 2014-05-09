/*
 * This file is part of Cockpit.
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * Cockpit is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Cockpit is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Cockpit; If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mock-auth.h"

#include "cockpit/cockpitenums.h"
#include "cockpit/cockpiterror.h"
#include "cockpit/cockpittest.h"
#include "ws/cockpitauth.h"
#include "websocket/websocket.h"

#include <string.h>

typedef struct {
  CockpitAuth *auth;
} Test;

static void
setup (Test *test,
       gconstpointer data)
{
  test->auth = mock_auth_new ("me", "this is the password");
}

static void
teardown (Test *test,
          gconstpointer data)
{
  g_object_unref (test->auth);
}

static void
on_ready_get_result (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
  GAsyncResult **retval = user_data;
  g_assert (retval != NULL);
  g_assert (*retval == NULL);
  *retval = g_object_ref (result);
}

static void
test_userpass_cookie_check (Test *test,
                            gconstpointer data)
{
  GAsyncResult *result = NULL;
  CockpitCreds *creds;
  GError *error = NULL;
  GHashTable *headers;
  GBytes *input;
  gchar *cookie;
  gchar *end;

  input = g_bytes_new_static ("me\nthis is the password", 23);
  cockpit_auth_login_async (test->auth, NULL, input, NULL, on_ready_get_result, &result);
  g_bytes_unref (input);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  headers = web_socket_util_new_headers ();
  creds = cockpit_auth_login_finish (test->auth, result, TRUE, headers, &error);
  g_object_unref (result);
  g_assert_no_error (error);
  g_assert (creds != NULL);

  g_assert_cmpstr ("me", ==, cockpit_creds_get_user (creds));
  g_assert_cmpstr ("this is the password", ==, cockpit_creds_get_password (creds));
  cockpit_creds_unref (creds);

  cookie = g_strdup (g_hash_table_lookup (headers, "Set-Cookie"));
  g_assert (cookie != NULL);

  end = strchr (cookie, ';');
  g_assert (end != NULL);
  end[0] = '\0';

  g_hash_table_insert (headers, g_strdup ("Cookie"), cookie);

  creds = cockpit_auth_check_cookie (test->auth, headers);
  g_assert (creds != NULL);

  g_assert_cmpstr ("me", ==, cockpit_creds_get_user (creds));
  g_assert_cmpstr ("this is the password", ==, cockpit_creds_get_password (creds));
  cockpit_creds_unref (creds);

  g_hash_table_destroy (headers);
}

static void
test_userpass_bad (Test *test,
                   gconstpointer data)
{
  GAsyncResult *result = NULL;
  GError *error = NULL;
  GHashTable *headers;
  CockpitCreds *creds;
  GBytes *input;

  input = g_bytes_new_static ("me\nbad", 6);
  cockpit_auth_login_async (test->auth, NULL, input, NULL, on_ready_get_result, &result);
  g_bytes_unref (input);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  headers = web_socket_util_new_headers ();
  creds = cockpit_auth_login_finish (test->auth, result, TRUE, headers, &error);
  g_object_unref (result);

  g_assert (creds == NULL);
  g_assert_error (error, COCKPIT_ERROR, COCKPIT_ERROR_AUTHENTICATION_FAILED);
  g_clear_error (&error);

  g_hash_table_destroy (headers);
}

static void
test_userpass_invalid (Test *test,
                       gconstpointer data)
{
  GAsyncResult *result = NULL;
  GError *error = NULL;
  GHashTable *headers;
  CockpitCreds *creds;
  GBytes *input;

  input = g_bytes_new_static ("me=bad", 6);
  cockpit_auth_login_async (test->auth, NULL, input, NULL, on_ready_get_result, &result);
  g_bytes_unref (input);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  headers = web_socket_util_new_headers ();
  creds = cockpit_auth_login_finish (test->auth, result, TRUE, headers, &error);
  g_object_unref (result);

  g_assert (creds == NULL);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);

  g_hash_table_destroy (headers);
}

static void
test_userpass_emptypass (Test *test,
                         gconstpointer data)
{
  GAsyncResult *result = NULL;
  GError *error = NULL;
  GHashTable *headers;
  CockpitCreds *creds;
  GBytes *input;

  input = g_bytes_new_static ("aaaaaa\n", 7);
  cockpit_auth_login_async (test->auth, NULL, input, NULL, on_ready_get_result, &result);
  g_bytes_unref (input);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  headers = web_socket_util_new_headers ();
  creds = cockpit_auth_login_finish (test->auth, result, TRUE, headers, &error);
  g_object_unref (result);

  g_assert (creds == NULL);
  g_assert_error (error, COCKPIT_ERROR, COCKPIT_ERROR_AUTHENTICATION_FAILED);
  g_clear_error (&error);

  g_hash_table_destroy (headers);
}

static void
test_headers_bad (Test *test,
                  gconstpointer data)
{
  GHashTable *headers;

  headers = web_socket_util_new_headers ();

  /* Bad version */
  g_hash_table_insert (headers, g_strdup ("Cookie"), g_strdup ("CockpitAuth=v=1;k=blah"));
  if (cockpit_auth_check_cookie (test->auth, headers))
      g_assert_not_reached ();

  /* Bad hash */
  g_hash_table_remove_all (headers);
  g_hash_table_insert (headers, g_strdup ("Cookie"), g_strdup ("CockpitAuth=v=2;k=blah"));
  if (cockpit_auth_check_cookie (test->auth, headers))
      g_assert_not_reached ();

  g_hash_table_destroy (headers);
}

int
main (int argc,
      char *argv[])
{
  cockpit_test_init (&argc, &argv);

  g_test_add ("/auth/userpass-header-check", Test, NULL, setup, test_userpass_cookie_check, teardown);
  g_test_add ("/auth/userpass-bad", Test, NULL, setup, test_userpass_bad, teardown);
  g_test_add ("/auth/userpass-invalid", Test, NULL, setup, test_userpass_invalid, teardown);
  g_test_add ("/auth/userpass-emptypass", Test, NULL, setup, test_userpass_emptypass, teardown);
  g_test_add ("/auth/headers-bad", Test, NULL, setup, test_headers_bad, teardown);

  return g_test_run ();
}
