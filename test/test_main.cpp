// Several test files in one program (CMakeLists.txt, nyxmap_test_main): each file keeps its own main(), renamed at
// compile time, and is run by name, one per process as CTest does it: <program> <test> [the test's own arguments].
// Without a name, the list.
#include <cstdio>
#include <cstring>
#include <vector>

struct TestMain
{
    const char *name;
    int (*run)(int, char **);
};
#include NYXMAP_TEST_TABLE // the renamed main()s (global), then kTests

int main(int argc, char **argv)
{
    if (argc > 1)
        for (const TestMain &test : kTests)
            if (std::strcmp(argv[1], test.name) == 0) {
                // The test sees its usual arguments: the program, then its own (argv[2]...)
                std::vector<char *> args{argv[0]};
                for (int k = 2; k < argc; ++k) args.push_back(argv[k]);
                args.push_back(nullptr);
                return test.run(argc - 1, args.data());
            }
    std::fprintf(stderr, "usage: %s <test> [arguments]\ntests:\n", argv[0]);
    for (const TestMain &test : kTests) std::fprintf(stderr, "  %s\n", test.name);
    return argc > 1 ? 2 : 0;
}
