#include "blaze.h"

String* string_new(const char* str) { return string_newz(str, strlen(str)); }
String* string_newz(const char* str, size_t len) {
    bassert(str, "expected non-null string");
    String* res = new(String);
    res->str = alloc(len+1);
    res->len = len;
    memcpy(res->str, str, res->len);
    res->str[res->len] = 0;
    return res;
}

String* string_clone(String* str) {
    bassert(str, "expected non-null string");
    return string_newz(str->str, str->len);
}

void string_free(String* str) {
    free(str->str);
    free(str);
}

String* string_add(String* lhs, String* rhs) {
    bassert(lhs && rhs, "expected non-null strings");
    String* res = new(String);
    res->str = alloc(lhs->len+rhs->len+1);
    res->len = lhs->len+rhs->len;
    memcpy(res->str, lhs->str, lhs->len);
    memcpy(res->str+lhs->len, rhs->str, rhs->len);
    res->str[res->len] = 0;
    return res;
}

void string_merge(String* base, String* rhs) {
    bassert(base && rhs, "expected non-null strings");
    base->str = ralloc(base->str, base->len+rhs->len+1);
    memcpy(base->str+base->len, rhs->str, rhs->len);
    base->str[base->len+rhs->len] = 0;
    base->len += rhs->len;
}
void string_mergec(String* base, char rhs) {
    String* rhss = string_newz(&rhs, 1);
    string_merge(base, rhss);
    string_free(rhss);
}
void string_merges(String* base, const char* rhs) {
    String* rhss = string_new(rhs);
    string_merge(base, rhss);
    string_free(rhss);
}
