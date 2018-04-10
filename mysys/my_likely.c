/* Copyright (c) 2018, MariaDB Corporation Ab.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/*
  Checks that my_likely/my_unlikely is correctly used

  Note that we can't use mysql_mutex or my_malloc here as these
  uses likely() macros and the likely_mutex would be used twice
*/

#include "mysys_priv.h"
#include <hash.h>
#include <m_ctype.h>

my_bool likely_inited= 0;

typedef struct st_likely_entry
{
  const char *key;
  uint key_length;
  uint line;
  ulonglong ok,fail;
} LIKELY_ENTRY;

static uchar *get_likely_key(LIKELY_ENTRY *part, size_t *length,
                             my_bool not_used __attribute__((unused)))
{
  *length= part->key_length;
  return (uchar*) part->key;
}

pthread_mutex_t likely_mutex;
HASH likely_hash;

void init_my_likely()
{
  /* Allocate big enough to avoid malloc calls */
  my_hash_init2(&likely_hash, 10000, &my_charset_bin,
                1024, 0, 0,
                (my_hash_get_key) get_likely_key, 0,
                free, HASH_UNIQUE);
  likely_inited= 1;
  pthread_mutex_init(&likely_mutex, MY_MUTEX_INIT_FAST);
}


void end_my_likely(FILE *out)
{
  LIKELY_ENTRY *entry;
  uint i;
  FILE *likely_file;
  my_bool do_close= 0;

  likely_inited= 0;

  if (!(likely_file= out))
  {
    if ((likely_file= my_fopen("/tmp/likely.out", O_TRUNC | O_WRONLY,
                                MYF(MY_WME))))
      do_close= 1;
    else
      likely_file= stderr;
  }

  for (i=0 ; (entry= (LIKELY_ENTRY *) my_hash_element(&likely_hash, i)) ; i++)
  {
    if (entry->fail > entry->ok)
      fprintf(likely_file,
              "%40.40s  line: %6u  ok: %8lld  fail: %8lld\n",
              entry->key, entry->line, entry->ok, entry->fail);
  }

  if (do_close)
    my_fclose(likely_file, MYF(MY_WME));
  pthread_mutex_destroy(&likely_mutex);
  my_hash_free(&likely_hash);
}


static LIKELY_ENTRY *my_likely_find(const char *file_name, uint line)
{
  char key[80], *pos;
  LIKELY_ENTRY *entry;
  uint length;

  if (!likely_inited)
    return 0;

  pos= strnmov(key, file_name, sizeof(key)-4);
  int3store(pos+1, line);
  length= (pos-key)+4;

  pthread_mutex_lock(&likely_mutex);
  if (!(entry= (LIKELY_ENTRY*) my_hash_search(&likely_hash, (uchar*) key,
                                              length)))
  {
    if (!(entry= (LIKELY_ENTRY *) malloc(sizeof(*entry) + length)))
      return 0;
    entry->key= (char*) (entry+1);
    memcpy((void*) entry->key, key, length);
    entry->key_length= length;
    entry->line= line;
    entry->ok= entry->fail= 0;

    if (my_hash_insert(&likely_hash, (void*) entry))
    {
      pthread_mutex_unlock(&likely_mutex);
      free(entry);
      return 0;
    }
  }
  pthread_mutex_unlock(&likely_mutex);
  return entry;
}


int my_likely_ok(const char *file_name, uint line)
{
  LIKELY_ENTRY *entry= my_likely_find(file_name, line);
  if (entry)
    entry->ok++;
  return 0;
}


int my_likely_fail(const char *file_name, uint line)
{
  LIKELY_ENTRY *entry= my_likely_find(file_name, line);
  if (entry)
    entry->fail++;
  return 0;
}
