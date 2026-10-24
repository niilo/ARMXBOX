#include "diagnostic.h"
#include <cstdio>
int main() { char report[16384]; int status = armxbox_fex_selftest(report, sizeof report); std::fputs(report, stdout); return status; }
