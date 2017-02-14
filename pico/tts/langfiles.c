#include "langfiles.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <stddef.h>
#include <ctype.h>

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

void lang_files_find(Lang_Filenames *fns, const char *langdir, const char *lang)
{
	assert(fns);
	assert(lang);
	assert(langdir);

	memset(fns, 0, sizeof(*fns));
	DIR *dir = opendir(langdir);
	if (!dir)
		return;
	
	int name_max = pathconf(langdir, _PC_NAME_MAX);
	if (name_max == -1)
		name_max = 4096; // Take a guess
	size_t len = offsetof(struct dirent, d_name) + name_max + 1;
	struct dirent *pentry = malloc(len), *retval = NULL;
	
	while (readdir_r(dir, pentry, &retval) == 0 && retval != NULL) {
		const char * const d_name = pentry->d_name;
		// We ommit hidden files, and the ".." and "." dirs
		if (starts_with(".", d_name, false))
			continue;

		// Did we find all the files already??
		if (fns->fname_ta && fns->fname_sq && fns->fname_utpp)
			break;

		if (!starts_with(lang, d_name, true))
			continue;

		if (!fns->fname_ta && ends_with("_ta.bin", d_name, true)) {
			fns->fname_ta = path_join(langdir, d_name);
		}
		else if (!fns->fname_sq && ends_with("_sq.bin", d_name, true)) {
			fns->fname_sq = path_join(langdir, d_name);
		}
		else if (!fns->fname_utpp && ends_with("_utpp.bin", d_name, true)) {
			fns->fname_utpp = path_join(langdir, d_name);
		}

		fprintf(stderr, "%s\n", pentry->d_name);
	}

	closedir(dir);
	free(pentry);
}

void lang_files_release(Lang_Filenames *fns)
{
	assert(fns);
	free(fns->fname_ta);
	free(fns->fname_sq);
	free(fns->fname_utpp);
	fns->fname_ta = NULL;
	fns->fname_sq = NULL;
	fns->fname_utpp = NULL;
}

