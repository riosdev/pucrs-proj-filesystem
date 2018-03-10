#include "filesystem.h"
#include "helper_functions.h"
#include "libdisksimul.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/**
 * @brief Format disk.
 *
 */
int fs_format() {
  int ret, i;
  struct root_table_directory root_dir;
  struct sector_data sector;

  if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 1)) != 0) {
    return ret;
  }

  memset(&root_dir, 0, sizeof(root_dir));

  root_dir.free_sectors_list = 1; /* first free sector. */

  ds_write_sector(0, (void *)&root_dir, SECTOR_SIZE);

  /* Create a list of free sectors. */
  memset(&sector, 0, sizeof(sector));

  for (i = 1; i < NUMBER_OF_SECTORS; i++) {
    if (i < NUMBER_OF_SECTORS - 1) {
      sector.next_sector = i + 1;
    } else {
      sector.next_sector = 0;
    }
    ds_write_sector(i, (void *)&sector, SECTOR_SIZE);
  }

  ds_stop();

  printf("Disk size %d kbytes, %d sectors.\n",
         (SECTOR_SIZE * NUMBER_OF_SECTORS) / 1024, NUMBER_OF_SECTORS);

  return 0;
}

/**
 * @brief Create a new file on the simulated filesystem.
 * @param input_file Source file path.
 * @param simul_file Destination file path on the simulated file system.
 * @return 0 on success.
 */
int fs_create(char *input_file, char *simul_file) { // should be done
  int ret, sector, pSector, pIndex;
  int size = 0;
  char buffer[1];
  FILE *file;
  struct sector_data data;
  struct file_dir_entry* entry;
  void *data2 = malloc(SECTOR_SIZE);

  fs_mkdir(simul_file); // create directories up to file

  if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0) {
    return ret;
  }

  hf_get_file_dir_entry_container(simul_file, &pIndex, data2, &pSector);
  ds_read_sector(pSector, data2, SECTOR_SIZE);
  if (pSector == 0) {
    entry = &(((struct root_table_directory *)data2)->entries[pIndex]);
  } else {
    entry = &(((struct table_directory *)data2)->entries[pIndex]);
  }
  entry->dir = 0;
  sector = entry->sector_start;
  if ((file = fopen(input_file, "r+b")) == NULL) {
    hf_error();
  }
  ds_read_sector(sector, &data, SECTOR_SIZE);
  while (fread(buffer, 1, 1, file) == 1) {
    if ((size != 0) && (size % 508 == 0)) {
      ds_write_sector(sector, (void *)&data, SECTOR_SIZE);
      sector = data.next_sector;
      ds_read_sector(sector, (void *)&data, SECTOR_SIZE);
    }
    data.data[size % 508] = *buffer;
    size++;
  }
  hf_invalidate_free_sector_list(data.next_sector);
  data.next_sector = 0;
  ds_write_sector(sector, (void *)&data, SECTOR_SIZE);
  entry->size_bytes = size;
  if(!pSector){//invalidate_free_sector_list updates root
    struct root_table_directory root;
    hf_get_root(&root);
    ((struct root_table_directory *)data2)->free_sectors_list = root.free_sectors_list;
    ((struct root_table_directory *)data2)->entries[pIndex] = *entry;
  }
  ds_write_sector(pSector, data2, SECTOR_SIZE);
  fclose(file);
  ds_stop();
  free(data2);
  return 0;
}

/**
 * @brief Read file from the simulated filesystem.
 * @param output_file Output file path.
 * @param simul_file Source file path from the simulated file system.
 * @return 0 on success.
 */
int fs_read(char *output_file, char *simul_file) {
  int ret, sector, pSector, pIndex;
  int size = 0;
  FILE *file;
  struct file_dir_entry entry;
  void *data2 = malloc(512);

  if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0) {
    return ret;
  }

  if (hf_get_file_dir_entry_container(simul_file, &pIndex, data2, &pSector) == -1)
    hf_error();
  ds_read_sector(pSector, data2, SECTOR_SIZE);
  if (pSector == 0) {
    entry = ((struct root_table_directory *)data2)->entries[pIndex];
  } else {
    entry = ((struct table_directory *)data2)->entries[pIndex];
  }
  if((file = fopen(output_file, "w+b")) == NULL){
    hf_error();
  }
  size = entry.size_bytes;
  sector = entry.sector_start;
  do{
    int counter = 0;
    struct sector_data data;
    ds_read_sector(sector, (void*)&data, SECTOR_SIZE);
    while(counter<508){
      if(--size < 0)
        break;
      fwrite(&(data.data[counter]), 1, 1, file);
      counter++;
    }
    sector = data.next_sector;
  } while (sector);
  free(data2);
  fclose(file);
  ds_stop();

  return 0;
}

/**
 * @brief Delete file from file system.
 * @param simul_file Source file path.
 * @return 0 on success.
 */
int fs_del(char *simul_file) { // should be done
  int ret;
  if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0) {
    return ret;
  }
  {
    int index, sector, sector_start;
    void *data = malloc(SECTOR_SIZE);
    if (hf_get_file_dir_entry_container(simul_file, &index, data, &sector) ==
        -1)
      hf_error();
    ds_read_sector(sector, data, SECTOR_SIZE);
    if (sector) {
      struct table_directory *td = (struct table_directory *)data;
      sector_start = td->entries[index].sector_start;
      td->entries[index].sector_start = 0;
    } else {
      struct root_table_directory *root = (struct root_table_directory *)data;
      sector_start = root->entries[index].sector_start;
    }
    {
      struct sector_data data2;
      struct root_table_directory root;
      int last_sector = hf_get_last_sector(sector_start);
      hf_get_root(&root);
      ds_read_sector(last_sector, (void *)&data2, SECTOR_SIZE);
      data2.next_sector = root.free_sectors_list;
      root.free_sectors_list = sector_start;
      ds_write_sector(last_sector, (void *)&data2, SECTOR_SIZE);
      ds_write_sector(0, (void *)&root, SECTOR_SIZE);
      if (!sector) {
        struct root_table_directory *root = (struct root_table_directory *)data;
        root->free_sectors_list = sector_start;
        root->entries[index].sector_start = 0;
      }
    }
    ds_write_sector(sector, data, SECTOR_SIZE);
    free(data);
  }

  ds_stop();

  return 0;
}

/**
 * @brief List files from a directory.
 * @param simul_file Source file path.
 * @return 0 on success.
 */
int fs_ls(char *dir_path) { // should be done
  int ret;
  if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0) {
    return ret;
  }

  {
    int index, sector;
    void *data = malloc(SECTOR_SIZE);
    struct file_dir_entry *entries;
    if (hf_is_root(dir_path)) {
      struct root_table_directory root;
      hf_get_root(&root);
      entries = root.entries;
      for (index = 0; index < 15; index++) {
        if (strlen(entries[index].name) == 0)
          break;
        if (entries[index].sector_start != 0) {
          if (entries[index].dir) {
            printf("%s\n", entries[index].name);
          } else {
            printf("%s\t%d bytes\n", entries[index].name,
                   entries[index].size_bytes);
          }
        }
      }
    } else {
      if (!hf_is_valid_path(dir_path))
        hf_error();
      if (hf_get_file_dir_entry_container(dir_path, &index, data, &sector) ==
          -1)
        hf_error();
      if (data == NULL) {
        hf_error();
      } else {
        struct table_directory td = *((struct table_directory *)data);
        entries = td.entries;
        for (index = 0; index < 16; index++) {
          if (strlen(entries[index].name) == 0)
            break;
          if (entries[index].sector_start != 0) {
            if (entries[index].dir) {
              printf("%s\n", entries[index].name);
            } else {
              printf("%s\t%d bytes\n", entries[index].name,
                     entries[index].size_bytes);
            }
          }
        }
      }
      free(data);
    }
  }

  ds_stop();

  return 0;
}

/**
 * @brief Create a new directory on the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_mkdir(char *directory_path) { // should be done
  int ret;
  if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0) {
    return ret;
  }
  if (hf_mkdir(directory_path) == -1)
    hf_error();

  ds_stop();

  return 0;
}

/**
 * @brief Remove directory from the simulated filesystem.
 * @param directory_path directory path.
 * @return 0 on success.
 */
int fs_rmdir(char *directory_path) { // should be done
  int ret;
  if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0) {
    return ret;
  }

  {
    int index, sector;
    void *data = malloc(SECTOR_SIZE);
    if (hf_is_root(directory_path))
      hf_error();
    if (hf_get_file_dir_entry_container(directory_path, &index, data,
                                        &sector) == -1)
      hf_error();
    if (data == NULL)
      hf_error();
    if (hf_get_count_items(((struct table_directory *)data)->entries, false) >
        0)
      hf_error();
    ds_read_sector(sector, data, SECTOR_SIZE);
    if (sector) // if not root
    {
      struct table_directory *td = (struct table_directory *)data;
      hf_push_free_sector(td->entries[index].sector_start);
      td->entries[index].sector_start = 0;
    } else {
      struct root_table_directory *root = (struct root_table_directory *)data;
      hf_push_free_sector(root->entries[index].sector_start);
      // hf_push_free_sector updated root
      ds_read_sector(sector, data, SECTOR_SIZE);
      root = (struct root_table_directory *)data;
      root->entries[index].sector_start = 0;
    }
    ds_write_sector(sector, data, SECTOR_SIZE);
    free(data);
  }

  ds_stop();

  return 0;
}

/**
 * @brief Generate a map of used/available sectors.
 * @param log_f Log file with the sector map.
 * @return 0 on success.
 */
int fs_free_map(char *log_f) {
  int ret, i, next;
  struct root_table_directory root_dir;
  struct sector_data sector;
  char *sector_array;
  FILE *log;
  int pid, status;
  int free_space = 0;
  char *exec_params[] = {"gnuplot", "sector_map.gnuplot", NULL};

  if ((ret = ds_init(FILENAME, SECTOR_SIZE, NUMBER_OF_SECTORS, 0)) != 0) {
    return ret;
  }

  /* each byte represents a sector. */
  sector_array = (char *)malloc(NUMBER_OF_SECTORS);

  /* set 0 to all sectors. Zero means that the sector is used. */
  memset(sector_array, 0, NUMBER_OF_SECTORS);

  /* Read the root dir to get the free blocks list. */
  ds_read_sector(0, (void *)&root_dir, SECTOR_SIZE);

  next = root_dir.free_sectors_list;

  while (next) {
    /* The sector is in the free list, mark with 1. */
    sector_array[next] = 1;

    /* move to the next free sector. */
    ds_read_sector(next, (void *)&sector, SECTOR_SIZE);

    next = sector.next_sector;

    free_space += SECTOR_SIZE;
  }

  /* Create a log file. */
  if ((log = fopen(log_f, "w")) == NULL) {
    perror("fopen()");
    free(sector_array);
    ds_stop();
    return 1;
  }

  /* Write the the sector map to the log file. */
  for (i = 0; i < NUMBER_OF_SECTORS; i++) {
    if (i % 32 == 0)
      fprintf(log, "%s", "\n");
    fprintf(log, " %d", sector_array[i]);
  }

  fclose(log);

  /* Execute gnuplot to generate the sector's free map. */
  pid = fork();
  if (pid == 0) {
    execvp("gnuplot", exec_params);
  }

  wait(&status);

  free(sector_array);

  ds_stop();

  printf("Free space %d kbytes.\n", free_space / 1024);

  return 0;
}
