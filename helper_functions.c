#include "filesystem.h"
#include "libdisksimul.h"
#include <stdlib.h>
#include <string.h>

int hf_get_root(struct root_table_directory *root) {
  return ds_read_sector(0, (void *)root, SECTOR_SIZE);
}

int hf_push_free_sector(int sector) {
  struct root_table_directory root;
  struct sector_data data;
  hf_get_root(&root);
  ds_read_sector(sector, (void *)&data, SECTOR_SIZE);
  memset(&data, 0, SECTOR_SIZE);
  data.next_sector = root.free_sectors_list;
  root.free_sectors_list = sector;
  ds_write_sector(0, (void *)&root, SECTOR_SIZE);
  ds_write_sector(sector, (void *)&data, SECTOR_SIZE);
  return 0;
}

int hf_get_last_sector(int sector) {
  struct sector_data data;
  ds_read_sector(sector, (void *)&data, SECTOR_SIZE);
  if (data.next_sector == 0)
    return sector;
  else
    return hf_get_last_sector(data.next_sector);
}

int hf_is_root(string path) {
  if (strlen(path) == 1) {
    if (path[0] == '/') {
      return true;
    }
  }
  return false;
}

int hf_invalidate_free_sector_list(int first_free_sector) {
  struct root_table_directory root;
  hf_get_root(&root);
  root.free_sectors_list = first_free_sector;
  ds_write_sector(0, (void *)&root, SECTOR_SIZE);
  return 0;
}

int hf_pop_free_sector() {
  int i;
  void *data = malloc(SECTOR_SIZE);
  ds_read_sector(0, data, SECTOR_SIZE);
  if ((i = *((unsigned int *)data)) == 0)
    return -1;
  {
    struct sector_data sector;
    ds_read_sector(i, (void *)&sector, SECTOR_SIZE);
    *((unsigned int *)data) = sector.next_sector;
    ds_write_sector(0, data, SECTOR_SIZE);
  }
  return i;
}

int hf_get_file_dir_index(struct file_dir_entry *entries, string name,
                          int is_root) {
  int length = is_root ? 15 : 16;
  int i = 0;
  for (i = 0; i < length; i++) {
    if ((strcmp(entries[i].name, name) == 0) && entries[i].sector_start != 0)
      return i;
  }
  return -1;
}

void hf_split_path(string *new, string old, int *count) {
  int i;
  int length = strlen(old);
  string str = calloc(length, 1);
  if (old[0] == '/') {
    i = 1;
    if (count != NULL)
      *count = 1;
    if (old[length - 1] == '/') {
      if (count != NULL)
        (*count)--;
    }
  } else {
    i = 0;
    if (count != NULL)
      *count = 0;
  }
  while (i < length) {
    if (old[i] == '/') {
      str[i] = '\0';
      if (count != NULL)
        (*count)++;
    } else
      str[i] = old[i];
    i++;
  }
  str[i] = '\0';
  *new = (old[0] == '/') ? (str + 1) : (str);
}

int hf_get_name_at_index(string path, int index, string *name) {
  int i, words;
  string str = NULL;
  hf_split_path(&str, path + 1, &words);
  for (i = 0; i < index; i++) {
    str += strlen(str) +1;
  }
  *name = str;
  return 0;
}

int hf_is_valid_path(string path) {
  int i, length;
  string name = NULL;
  string str = NULL;
  if (path[0] != '/')
    return false;
  hf_split_path(&str, path, &length);
  for (i = 0; i < length; i++) {
    hf_get_name_at_index(path, i, &name);
    if (strlen(name) >= 20)
      return false;
  }
  return true;
}

int hf_get_count_items(struct file_dir_entry *entries, int is_root) {
  int i, count;
  int length = is_root ? 15 : 16;
  for (count = i = 0; i < length; i++) {
    if (strlen(entries[i].name) == 0)
      break;
    if (entries[i].sector_start > 0)
      count++;
  }
  return count;
}

int hf_get_first_available(struct file_dir_entry *entries, int is_root) {
  int i;
  int length = is_root ? 15 : 16;
  for (i = 0; i < length; i++) {
    if (entries[i].sector_start == 0 || strlen(entries[i].name) == 0)
      return i;
  }
  return -1;
}

int hf_gi(string name, int *index, void *data, int *sector, int is_root) {
  static int t_sector = 0;
  struct root_table_directory *root;
  struct table_directory *td;
  // struct file_dir_entry *entry;
  if (is_root == 1) {
    root = (struct root_table_directory *)data;
    if ((*index = hf_get_file_dir_index(root->entries, name, true)) == -1)
      return -1;
    *sector = root->entries[*index].sector_start;
    if (!(*sector))
      return -1;
    if (!(root->entries[*index].dir))
      data = NULL;
    else {
      t_sector = *sector;
      ds_read_sector(*sector, data, SECTOR_SIZE);
    }
  } else {
    td = (struct table_directory *)data;
    if ((*index = hf_get_file_dir_index(td->entries, name, false)) == -1)
      return -1;
    *sector = t_sector;
    t_sector = td->entries[*index].sector_start;
    if (!t_sector)
      return -1;
    if (!(td->entries[*index].dir))
      data = NULL;
    else
      ds_read_sector(t_sector, data, SECTOR_SIZE);
  }
  return 0;
}

int hf_get_file_dir_entry_container(string directory_path, int *index,
                                    void *data, int *sector) {
  int i, length;
  string str = NULL;
  hf_split_path(&str, directory_path, &length);
  hf_get_root(data);
  for (*sector = i = 0; i < length; i++) {
    string name = NULL;
    hf_get_name_at_index(directory_path, i, &name);
    if (data == NULL)
      return -1;
    if (hf_gi(name, index, data, sector, i == 0) == -1)
      return -1;
  }
  if(length == 1)
    *sector = 0;
  return 0;
}

int hf_mkdir_rec(string path, int setor) {
  int index, f_setor;
  void *data = malloc(SECTOR_SIZE);
  int length = strlen(path);
  struct file_dir_entry *entries;
  if (length <= 0)
    return setor;
  ds_read_sector(setor, data, SECTOR_SIZE);
  if (!setor) {
    entries = ((struct root_table_directory *)data)->entries;
  } else
    entries = ((struct table_directory *)data)->entries;
  if ((index = hf_get_file_dir_index(entries, path, setor == 0)) >= 0) {
    if (!entries[index].dir)
      return -1;
    return hf_mkdir_rec(path + strlen(path) + 1, entries[index].sector_start);
  }
  if ((index = hf_get_first_available(entries, !setor)) == -1)
    return -1;
  {
    struct file_dir_entry entry;
    f_setor = hf_pop_free_sector();
    if(!setor){ //root was updated on pop_free_sector
      ds_read_sector(0, data, SECTOR_SIZE);
      entries = ((struct root_table_directory *)data)->entries;
    }
    if (f_setor == -1)
      return -1;
    entry.dir = 1;
    entry.size_bytes = 0;
    entry.sector_start = f_setor;
    strcpy(entry.name, path);
    entries[index] = entry;
  }
  ds_write_sector(setor, data, SECTOR_SIZE);
  return hf_mkdir_rec(path + strlen(path) + 1, f_setor);
}

int hf_mkdir(string path) {
  string str = NULL;
  if (strlen(path) <= 1)
    return -1;
  hf_split_path(&str, path, NULL);
  if (hf_mkdir_rec(str, 0) == -1)
    return -1;
  return 0;
}

int hf_error() // OKAY
{
  ds_stop();
  exit(-1);
}