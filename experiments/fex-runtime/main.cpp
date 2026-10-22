#include "diagnostic.h"
#include <cstdio>
int main() { char report[8192]; int status = armxbox_fex_selftest(report, sizeof report); std::fputs(report, stdout); return status; }
