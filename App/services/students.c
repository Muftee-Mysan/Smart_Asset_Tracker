/**
 * @file    students.c
 * @brief   UID -> student lookup over the generated, sorted table.
 */
#include "students.h"
#include <string.h>

/* Same order as gen_students.py: by UID length, then UID bytes. */
static int compare(const uint8_t *uid, uint8_t uid_len, const student_t *s)
{
    if (uid_len != s->uid_len) {
        return (uid_len < s->uid_len) ? -1 : 1;
    }
    return memcmp(uid, s->uid, uid_len);
}

const student_t *students_lookup(const uint8_t *uid, uint8_t uid_len)
{
    if ((uid_len == 0U) || (uid_len > STUDENT_UID_MAX)) {
        return NULL;
    }
    int32_t lo = 0;
    int32_t hi = (int32_t)g_student_count - 1;
    while (lo <= hi) {
        const int32_t mid = lo + (hi - lo) / 2;
        const int c = compare(uid, uid_len, &g_students[mid]);
        if (c == 0) {
            return &g_students[mid];
        }
        if (c < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}
