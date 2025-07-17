#include "Testing.h"

int main()
{
    TestBuilder tester;
    TestLoader load_tests; 

    std::vector<TestCase> cases = load_tests();

    int total_tests = cases.size();
    int passed_tests = 0;

    for (auto test_case : cases)
    {
        passed_tests += tester.Test(test_case); // adds 1 for every passed test
    }

    std::cout << "\n\n" << "All test complete\n" << "Passed " << passed_tests << " / " << total_tests << " tests.\n";
        
}