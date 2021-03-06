#pragma once
#include <fcntl.h>

enum {
	FILE_COMPARE_FAIL = -1,
	FILE_COMPARE_SAME,
	FILE_COMPARE_DIFFERENT
};

#define DEF_MODE            S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH

int file_operation_compare(const char *file1, const char *file2);
