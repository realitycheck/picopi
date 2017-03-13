#ifndef LANG_FILENAMES_H
#define LANG_FILENAMES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sLang_Filenames {	
	char *fname_ta;
	char *fname_sg;
	char *fname_utpp;
} Lang_Filenames;

void lang_files_find(Lang_Filenames *fns, const char *lang_dir, const char *lang);

void lang_files_release(Lang_Filenames *fns);

#ifdef __cplusplus
}
#endif

#endif
