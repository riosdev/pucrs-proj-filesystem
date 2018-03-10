#ifndef HELPER_FUNCTIONS
#define HELPER_FUNCTIONS

#include "filesystem.h"

int hf_get_root(struct root_table_directory *root);
int hf_push_free_sector(int sector);
int hf_get_last_sector(int sector);
int hf_is_root(string path);
int hf_invalidate_free_sector_list(int first_free_sector);
int hf_pop_free_sector();
int hf_get_file_dir_index(struct file_dir_entry *entries, string name, int is_root);
void hf_split_path(string new, string old, int *count);
int hf_get_name_at_index(string path, int index, string name);
int hf_is_valid_path(string path);
int hf_get_count_items(struct file_dir_entry *entries, int is_root);
int hf_get_first_available(struct file_dir_entry *entries, int is_root);
int hf_gi(string name, int *index, void *data, int *sector, int is_root);
int hf_get_file_dir_entry_container(string directory_path, int *index, void *data, int *sector);
int hf_mkdir_rec(string path, int setor);
int hf_mkdir(string path);
int hf_error();

#endif