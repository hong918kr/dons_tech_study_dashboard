// CppUTest entry point: runs every TEST in every linked file (-v, -g Group, -n Name, -ojunit).
#include "CppUTest/CommandLineTestRunner.h"

int main(int argc, char **argv)
{
    return CommandLineTestRunner::RunAllTests(argc, argv);
}
