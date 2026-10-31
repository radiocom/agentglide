/* Parse real TOML, emit only the value types supported by the v1 schema.
   Validation of names, required fields and client options lives in agentglide.ps1. */
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "../deps/tomlc99/toml.h"
static void string(const char *s) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if (*p < 32) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}
static int table(toml_table_t *t, int depth) {
    if (depth > 8) return 0;
    putchar('{');
    for (int i = 0;; i++) {
        const char *key = toml_key_in(t, i);
        if (!key) break;
        if (i) putchar(',');
        string(key); putchar(':');
        toml_table_t *child = toml_table_in(t, key);
        toml_datum_t v;
        if (child) { if (!table(child, depth + 1)) return 0; }
        else if ((v = toml_string_in(t, key)).ok) { string(v.u.s); free(v.u.s); }
        else if ((v = toml_bool_in(t, key)).ok) fputs(v.u.b ? "true" : "false", stdout);
        else if ((v = toml_int_in(t, key)).ok) printf("%lld", (long long)v.u.i);
        else return 0;
    }
    putchar('}'); return 1;
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) return 2;
    FILE *f = _wfopen(argv[1], L"rb");
    if (!f) { fputs("Cannot read vfs.toml\n", stderr); return 2; }
    /* Accept a UTF-8 BOM from Windows editors. */
    unsigned char bom[3];
    if (fread(bom, 1, 3, f) != 3 || bom[0] != 239 || bom[1] != 187 || bom[2] != 191) rewind(f);
    char error[256];
    toml_table_t *t = toml_parse_file(f, error, sizeof(error)); fclose(f);
    /* Do not echo parser text: malformed config could contain credentials. */
    if (!t) { fputs("Invalid TOML syntax in vfs.toml\n", stderr); return 2; }
    int ok = table(t, 0); toml_free(t);
    if (!ok) { fputs("Unsupported TOML value type (v1 supports tables, strings, integers, booleans)\n", stderr); return 2; }
    return 0;
}
