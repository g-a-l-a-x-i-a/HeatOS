#include "terminal.h"
#include "mamu.h"
#include "catnake.h"
#include "kat.h"

void cmd_mamu(const char *args) {
    mamu_run(args ? args : "");
}

void cmd_catnake(const char *args) {
    (void)args;
    catnake_run();
}

void cmd_kat(const char *args) {
    kat_run(args);
}
