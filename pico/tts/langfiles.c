#include "langfiles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <tchar.h> 
#include <strsafe.h>
#pragma comment(lib, "User32.lib")
#define strcasecmp(A, B) _stricmp(A, B)
/* Emulate stdbool.h */
typedef int bool;
#define false 0
#define true 1

#else
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#endif

static bool starts_with(const char *prefix, const char *haystack, bool case_insensitive)
{
	int i = 0;
	while (prefix[i] && (prefix[i] == haystack[i] || (
		case_insensitive && toupper(prefix[i]) == toupper(haystack[i])))) {
			i++;
	}
	return prefix[i] == '\0';
}

static bool ends_with(const char *postfix, const char *haystack, bool case_insensitive)
{
	size_t l1 = strlen(postfix);
	size_t l2 = strlen(haystack);
	if (l1 > l2)
		return false;
	if (case_insensitive) {
		return strcasecmp(postfix, haystack + (l2 - l1)) == 0;
	}
	else {
		return strcmp(postfix, haystack + (l2 - l1)) == 0;
	}
}

static char * path_join(const char *dir, const char *filename)
{
	size_t len1 = strlen(dir);
	size_t len2 = strlen(filename);
	size_t buf_size = len1 + len2 + 2;
	char * buf = (char *) malloc(buf_size);
	char * p = buf;
	memcpy(buf, dir, len1 + 1);
	p += len1;

	// Add dir separator if needed
	if (len1 > 0 &&  dir[len1-1] != '/')
		strcat(p, "/");
	strcat(p, filename);
	return buf;
}

#ifdef _WIN32

void lang_files_find(Lang_Filenames *fns, const char *langdir, const char *lang)
{
	WIN32_FIND_DATA ffd;
	TCHAR szDir[MAX_PATH];
	HANDLE hFind = INVALID_HANDLE_VALUE;
	DWORD dwError = 0;
	assert(fns);
	assert(lang);
	assert(langdir);

	memset(fns, 0, sizeof(*fns));
	
	// Prepare string for use with FindFile functions.  First, copy the
	// string to a buffer, then append '\*' to the directory name.

	StringCchCopy(szDir, MAX_PATH, langdir);
	StringCchCat(szDir, MAX_PATH, TEXT("\\*"));

	// Find the first file in the directory.
	hFind = FindFirstFile(szDir, &ffd);

	if (INVALID_HANDLE_VALUE == hFind) {
		return;
	}

	// List all the files in the directory with some info about them.
	do
	{
		const TCHAR * const d_name = ffd.cFileName;

		// skip directories...
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		// We omit hidden files, and the ".." and "." dirs
		if (starts_with(".", d_name, false))
			continue;

		// Did we find all the files already??
		if (fns->fname_ta && fns->fname_sg && fns->fname_utpp)
			break;

		if (!starts_with(lang, d_name, true))
			continue;

		if (!fns->fname_ta && ends_with("_ta.bin", d_name, true)) {
			fns->fname_ta = path_join(langdir, d_name);
		}
		else if (!fns->fname_sg && ends_with("_sg.bin", d_name, true)) {
			fns->fname_sg = path_join(langdir, d_name);
		}
		else if (!fns->fname_utpp && ends_with("_utpp.bin", d_name, true)) {
			fns->fname_utpp = path_join(langdir, d_name);
		}

	} while (FindNextFile(hFind, &ffd) != 0);

	FindClose(hFind);
}

#else

void lang_files_find(Lang_Filenames *fns, const char *langdir, const char *lang)
{
	struct dirent *pentry = NULL;
	assert(fns);
	assert(lang);
	assert(langdir);

	memset(fns, 0, sizeof(*fns));
	DIR *dir = opendir(langdir);
	if (!dir)
		return;

	while ((pentry = readdir(dir)) != NULL) {
		const char * const d_name = pentry->d_name;
		// We omit hidden files, and the ".." and "." dirs
		if (starts_with(".", d_name, false))
			continue;

		// Did we find all the files already??
		if (fns->fname_ta && fns->fname_sg && fns->fname_utpp)
			break;

		if (!starts_with(lang, d_name, true))
			continue;

		if (!fns->fname_ta && ends_with("_ta.bin", d_name, true)) {
			fns->fname_ta = path_join(langdir, d_name);
		}
		else if (!fns->fname_sg && ends_with("_sg.bin", d_name, true)) {
			fns->fname_sg = path_join(langdir, d_name);
		}
		else if (!fns->fname_utpp && ends_with("_utpp.bin", d_name, true)) {
			fns->fname_utpp = path_join(langdir, d_name);
		}
	}

	closedir(dir);
}
#endif

void lang_files_release(Lang_Filenames *fns)
{
	assert(fns);
	free(fns->fname_ta);
	free(fns->fname_sg);
	free(fns->fname_utpp);
	fns->fname_ta = NULL;
	fns->fname_sg = NULL;
	fns->fname_utpp = NULL;
}

