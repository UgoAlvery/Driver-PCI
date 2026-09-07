#ifndef EDU_CHAR_H
#define EDU_CHAR_H

#include "edu_dev.h"

int edu_char_global_init(void);
void edu_char_global_exit(void);

int edu_char_init(struct edu_dev *edu);
void edu_char_cleanup(struct edu_dev *edu);

#endif