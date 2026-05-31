#include "cli.h"
#include <QtGlobal>
#include <cstdio>

int main(int, char **) {
    std::puts(qPrintable(eddy::versionString()));
    return 0;
}
