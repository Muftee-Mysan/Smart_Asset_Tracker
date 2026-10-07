/**
 * @file    students.h
 * @brief   UID -> student lookup over the generated, sorted table
 *          (App/data/students_table.c, from tools/students.csv).
 */
#ifndef APP_STUDENTS_H
#define APP_STUDENTS_H

#include <stdint.h>

#define STUDENT_UID_MAX     7U
#define STUDENT_ID_LEN      12U     /* 11 chars + NUL */
#define STUDENT_NAME_LEN    21U     /* 20 chars + NUL */

typedef struct {
    uint8_t uid_len;                /* 4 or 7 */
    uint8_t uid[STUDENT_UID_MAX];   /* zero-padded */
    char student_id[STUDENT_ID_LEN];
    char name[STUDENT_NAME_LEN];
} student_t;

extern const student_t g_students[];
extern const uint16_t g_student_count;

/* Binary search. Returns NULL for an unregistered card. */
const student_t *students_lookup(const uint8_t *uid, uint8_t uid_len);

#endif /* APP_STUDENTS_H */
